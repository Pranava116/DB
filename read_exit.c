#include<stdio.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include<stdbool.h>
#define COLUMN_USERNAME_LENGTH 32
#define COLUMN_EMAIL_LENGTH 255
#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0) -> Attribute)
#define TABLE_MAX_PAGES 100

//the above line is a macro that gives the size of the attribute in the struct
typedef enum{
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED
}MetaCommandResult;
typedef struct{
  uint32_t id;
  char username[COLUMN_USERNAME_LENGTH +1];
  char email[COLUMN_EMAIL_LENGTH+1];
}Row;
typedef struct{
  int file_desc;
  uint32_t file_length;
  void* pages[TABLE_MAX_PAGES];
}Pager;
typedef struct{
  uint32_t num_rows;
  Pager* pager;
}Table;
typedef struct{
  Table* table;
  uint32_t row_num;
  bool end_of_page;
}Cursor;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
//in disk we dont seperate and store the valaues of id, username, email instead we store raw bytes and to know when the next values stat we use offsets
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
const uint32_t PAGE_SIZE = 4096;

const uint32_t ROWS_PER_PAGE = PAGE_SIZE/ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;
//pages are fied blocks of memory where rows are stored
typedef enum{
  STATEMENT_INSERT,
  STATEMENT_SELECT,
}StatementType;
typedef struct{
  StatementType type;
  Row row_to_insert;
}Statement;
typedef enum{EXECUTE_SUCCESS, EXECUTE_TABLE_FULL}ExecutResult;
typedef enum {PREPARE_SUCCESS, PREPARE_UNRECOGNIZED_STATEMENT,PREPARE_SYNTAX_ERROR, PREPARE_STRING_TOO_LONG, PREPARE_ID_NEGETIVE} PrepareResult;
typedef struct{
  char* buffer;
  size_t buffer_length;
  size_t input_length;
}InputBuffer;
//used to allcate memory to the user input
InputBuffer* new_input_buffer(){
  InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;
  return  input_buffer;
}
Cursor* table_start(Table* table ){
  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->row_num = 0;
  cursor->end_of_page = false;
  return cursor;
}
Cursor* table_end(Table* table ){
  Cursor* cursor= malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->row_num = table->num_rows;
  cursor->end_of_page = true;
  return cursor;
}

void deserialize_values(void* source, Row* destination){
  memcpy(&(destination->id), (char*)source + ID_OFFSET, ID_SIZE);
  memcpy(destination->username, (char*)source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(destination->email, (char*)source + EMAIL_OFFSET, EMAIL_SIZE);
}
// so what er do is we take ths tructed data like struct and convert them to raw bytes to store in memory that is why the desitnation type is void
void serialize_values(Row* source, void* destination){
  memcpy((char*)destination + ID_OFFSET, &(source->id), ID_SIZE);
  memcpy((char*)destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
  memcpy((char*)destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}
void* get_page(Pager* pager, uint32_t page_num){
        if(page_num >TABLE_MAX_PAGES){
        printf("Tried to fetch page out of bound");
        exit(EXIT_FAILURE);
        }
        if(pager->pages[page_num] == NULL){
        void* page = malloc(PAGE_SIZE);
        uint32_t num_pages = pager->file_length /PAGE_SIZE;
        if(pager->file_length % PAGE_SIZE){
        num_pages += 1;
        }
        if(page_num <= num_pages){
        lseek(pager->file_desc, page_num * PAGE_SIZE, SEEK_SET);
        ssize_t bytes_read = read(pager->file_desc, page, PAGE_SIZE);
        if(bytes_read == -1){
        printf("Error reading fiule \n");
        exit(EXIT_FAILURE);
        }
        }
        pager->pages[page_num]  = page;
        }
        return pager->pages[page_num];

        }

void* cursor_adv(Cursor* cursor){
  cursor->row_num++;
  if(cursor->row_num >=cursor->table->num_rows){
    cursor->end_of_page = true;
  }
}
void* cursor_value(Cursor* cursor){
  uint32_t row_num = cursor->row_num;
  uint32_t page_num = row_num/ROWS_PER_PAGE;
  void* page = get_page(cursor->table->pager, page_num);
  uint32_t row_offset = row_num%ROWS_PER_PAGE;
  uint32_t byte_offset = row_offset * ROW_SIZE;
  return (char*) page + byte_offset;
}
void printf_prompt(){
  printf("db > ");

}
void read_input(InputBuffer* input_buffer){
  ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
  if(bytes_read <= 0){
    printf("Erro reading input \n");
    exit(EXIT_FAILURE);
  }
  input_buffer->input_length = bytes_read -1;
  input_buffer->buffer[bytes_read-1] = 0;
}

void close_input_buffer(InputBuffer* input_buffer){
  free(input_buffer->buffer);
  free(input_buffer);
}
void pager_flush(Pager* pager, uint32_t page_num, uint32_t size){
    if(pager->pages[page_num] == NULL){
        printf("Tried to flush null page");
        exit(EXIT_FAILURE);
    }
    off_t offset = lseek(pager->file_desc, page_num * PAGE_SIZE, SEEK_SET);
    if(offset == -1){
        printf("error seeking \n");
        exit(EXIT_FAILURE);

    }
    ssize_t bytes_written = write(pager->file_desc, pager->pages[page_num], size);
    if(bytes_written == -1){
        printf("Error writing \n");
        exit(EXIT_FAILURE);
    }
}
void db_close(Table* table){
    Pager* pager = table->pager;
    uint32_t num_full_pages = table->num_rows/ROWS_PER_PAGE;
    for(uint32_t i =0;i<num_full_pages;i++){
        if(pager->pages[i] == NULL){
            continue;
        }
        pager_flush(pager, i, PAGE_SIZE);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }
    uint32_t num_add_rows = table->num_rows%ROWS_PER_PAGE;
    if(num_add_rows > 0){
        uint32_t page_num = num_full_pages;
        if(pager->pages[page_num] != NULL){
            pager_flush(pager, page_num, num_add_rows * ROW_SIZE);
            free(pager->pages[page_num]);
            pager->pages[page_num] =  NULL;
        }
    }
    int result = close(pager->file_desc);
    if(result == -1){
        printf("error closing db file");
        exit(EXIT_FAILURE);
    }
    for(uint32_t i = 0;i<TABLE_MAX_PAGES; i++){
        void* page = pager->pages[i];
        if(page){
            free(page);
            pager->pages[i] = NULL;
        }
    }
    free(table);
    free(pager);
}
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table){
  if(strcmp(input_buffer->buffer,".exit") == 0){
      db_close(table);
    exit(EXIT_SUCCESS);
  }else{
    return META_COMMAND_UNRECOGNIZED;
  }
}

PrepareResult prepare_insert(InputBuffer* input_buffer,Statement* statement ){
  if(strncmp(input_buffer->buffer, "insert", 6) == 0){
    statement->type = STATEMENT_INSERT;
    char* keyword = strtok(input_buffer->buffer, " ");
    char* id_string = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");
    if(id_string == NULL || username == NULL || email == NULL){
      return PREPARE_SYNTAX_ERROR;
    }
    int id = atoi(id_string);
    if(id<0){
      return PREPARE_ID_NEGETIVE;
    }
    if(strlen(username) > COLUMN_USERNAME_LENGTH){
      return PREPARE_STRING_TOO_LONG;
    }
    if(strlen(email) > COLUMN_EMAIL_LENGTH){
      return PREPARE_STRING_TOO_LONG;

    }
    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);
    return PREPARE_SUCCESS;
  }
  return PREPARE_UNRECOGNIZED_STATEMENT;
}
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){
  if(strncmp(input_buffer->buffer, "insert", 6) == 0){
    return prepare_insert(input_buffer, statement);
  }
  if(strncmp(input_buffer->buffer, "select", 6) == 0){
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
  }
  return PREPARE_UNRECOGNIZED_STATEMENT;
}
Pager* pager_open_file(const char* filename){
    int fd = open(filename, O_RDWR|O_CREAT, S_IWUSR|S_IRUSR );
    if(fd == -1){
        printf("Unable to open file \n");
        exit(EXIT_FAILURE);
    }
    off_t file_length = lseek(fd, 0, SEEK_END);
    Pager* pager = malloc(sizeof(Pager));
    pager->file_desc = fd;
    pager->file_length= file_length;
    for(uint32_t i = 0;i<TABLE_MAX_PAGES; i++){
        pager->pages[i] = NULL;
    }
    return pager;
}
Table* db_open(const char* filename){
    Pager* pager = pager_open_file(filename);
    uint32_t num_rows = pager->file_length /ROW_SIZE;
  Table* table = (Table*)malloc(sizeof(Table));
  table->pager =  pager;
  table->num_rows = num_rows;
  return table;
}
void free_table(Table* table){
  for(int i = 0;i<TABLE_MAX_PAGES; i++){
    free(table->pager->pages[i]);
  }
  free(table->pager);
  free(table);
}
ExecutResult execute_insert(Statement* statement, Table* table){
  if(table->num_rows >= TABLE_MAX_ROWS){
    return EXECUTE_TABLE_FULL;
  }
  Cursor* cursor = table_end(table);
  Row* row_to_insert = &(statement->row_to_insert);
  serialize_values(row_to_insert, cursor_value(cursor));
  table->num_rows++;
  return EXECUTE_SUCCESS;
}
void print_row(Row* row){
  printf("(%d, %s, %s) \n", row->id, row->username, row->email);
}
ExecutResult execute_select(Table* table){
  Cursor* cursor = table_start(table);
  Row row;
  while((!cursor->end_of_page)){
    deserialize_values(cursor_value(cursor), &row);
    print_row(&row);
    cursor_adv(cursor);
  }
  return EXECUTE_SUCCESS;
}
ExecutResult execute_Statement(Statement* statement, Table* table){

  switch(statement->type){
    case (STATEMENT_SELECT):
    printf("This is an select statement");
      return execute_select(table);
    break;
    case (STATEMENT_INSERT):
    printf("This is a insert statement");
      return execute_insert(statement, table);
    break;
  }
  return EXECUTE_SUCCESS;
}
int main(int argc, char* argv[]){
  InputBuffer* input_buffer = new_input_buffer();
  if(argc < 2){
      printf("Must supply database filename \n");
      exit(EXIT_FAILURE);
  }
  char* filename = argv[1];
  Table* table = db_open(filename);
  while (true) {
    printf_prompt();
    read_input(input_buffer);
      if(input_buffer->buffer[0] == '.'){
        switch(do_meta_command(input_buffer, table)){
          case (META_COMMAND_SUCCESS):
          continue;
          case (META_COMMAND_UNRECOGNIZED):
          printf("Unrecognized command \n");
          continue;
        }
      }
      Statement statement;
      switch(prepare_statement(input_buffer, &statement)){
        case (PREPARE_SUCCESS):
        break;
        case (PREPARE_UNRECOGNIZED_STATEMENT):
        printf("unrcognized keyword at start of '%s' \n", input_buffer->buffer);
        continue;
      case (PREPARE_SYNTAX_ERROR):
      printf("Synteax error \n");
      continue;
      case (PREPARE_STRING_TOO_LONG):
      printf("Too long of a string mate \n");
      continue;
        case (PREPARE_ID_NEGETIVE):
      printf("ID cannot be negetive \n");
      continue;
      }
    switch(execute_Statement(&statement, table)){
      case(EXECUTE_SUCCESS):
      printf("Executed \n");
      break;
      case(EXECUTE_TABLE_FULL):
      printf("Error: Table full \n");
      break;
    }
  }
}
