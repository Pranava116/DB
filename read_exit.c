#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
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

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
//in disk we dont seperate and store the valaues of id, username, email instead we store raw bytes and to know when the next values stat we use offsets
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_OFFSET+USERNAME_OFFSET+EMAIL_OFFSET;
const uint32_t PAGE_SIZE = 4096;

const uint32_t ROWS_PER_PAGE = PAGE_SIZE/ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

typedef struct{
  uint32_t num_rows;
  Pager* pages[TABLE_MAX_PAGES];
}Table;
//pages are fied blocks of memory where rows are stored 
typedef enum{
  STATEMENT_INSERT,
  STATEMENT_SELECT,
}StatementType;
typedef struct{
  StatementType type;
  Row row_to_insert;
}Statement;
typedef struct{
  int file_desc;
  uint32_t file_lenght;
  void* pages[TABLE_MAX_PAGES];
}Pager;
typedef enum{EXECUTE_SUCCESS, EXECUTE_TABLE_FULL}ExecutResult;
typedef enum {PREPARE_SUCCESS, PREPARE_UNRECOGNIZED_STATEMENT,PREPARE_SYNTAX_ERROR, PREPARE_STRING_TOO_LONG, PREPARE_ID_NEGETIVE} PrepareResult;
typedef struct{
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
}InputBuffer;
//used to allcate memory to the user input 
InputBuffer* new_input_buffer(){
  InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;
  return  input_buffer;
}


void serialize_values(Row* source, void* destination){
  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
  memcpy(destination +USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
  memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}
// so what er do is we take ths tructed data like struct and convert them to raw bytes to store in memory that is why the desitnation type is void 
//
void deserialize_values(void* source, Row* destination){
  memcpy(&(destination->id), source+ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), source+USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), source+EMAIL_OFFSET, EMAIL_SIZE);
}


void* row_slot(Table* table, uint32_t row_num){
  uint32_t page_num = row_num/ROWS_PER_PAGE;
  void* page = table->pages[page_num];
  if(page == NULL){
    page = table->pages[page_num] = malloc(PAGE_SIZE);
  }
  uint32_t row_offset = row_num%ROWS_PER_PAGE;
  uint32_t byte_offset = row_offset * ROW_SIZE;
  return page + byte_offset;
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
MetaCommandResult do_meta_command(InputBuffer* input_buffer){
  if(strcmp(input_buffer->buffer,".exit") == 0){
    close_input_buffer(input_buffer);
    exit(EXIT_SUCCESS);
  }else{
    return META_COMMAND_UNRECOGNIZED;
  }
}

PrepareResult prepare_insert(InputBuffer* input_buffer,Statement* statement ){
  if(strncmp(input_buffer->buffer, "insert", 6) == 0){
    statement->type = STATEMENT_INSERT;
    char* keywork = strtok(input_buffer->buffer, " ");
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
}
Table* new_table(){
  Table* table = (Table*)malloc(sizeof(Table));
  table->num_rows = 0;
  for(uint32_t i =0;i<TABLE_MAX_PAGES; i++){
    table->pages[i] = NULL;
  }
  return table;
}
void* free_table(Table* table){
  for(int i = 0;table->pages[i]; i++){
    free(table->pages[i]);
    free(table);
  }
}
ExecutResult execute_insert(Statement* statement, Table* table){
  if(table->num_rows >= TABLE_MAX_ROWS){
    return EXECUTE_TABLE_FULL;
  }
  Row* row_to_insert = &(statement->row_to_insert);
  serialize_values(row_to_insert, row_slot(table, table->num_rows));
  table->num_rows++;
  return EXECUTE_SUCCESS;
}
void* print_row(Row* row){
  printf("(%d, %s, %s) \n", row->id, row->username, row->email);
}
ExecutResult execute_select(Statement* staatement, Table* table){
  Row row;
  for(uint32_t i = 0;i<table->num_rows; i++){
    deserialize_values(row_slot(table, i), &row);
    print_row(&row);
  }
  return EXECUTE_SUCCESS;
}
ExecutResult execute_Statement(Statement* statement, Table* table){

  switch(statement->type){
    case (STATEMENT_SELECT):
    printf("This is an select statement");
      return execute_select(statement, table);
    break;
    case (STATEMENT_INSERT):
    printf("This is a insert statement");
      return execute_insert(statement, table);
    break;
  }
}
int main(int argc, char* argv[]){
  InputBuffer* input_buffer = new_input_buffer();
  Table* table = new_table();
  while (true) {
    printf_prompt();
    read_input(input_buffer);
      if(input_buffer->buffer[0] == '.'){
        switch(do_meta_command(input_buffer)){
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
      printf("Too lonf of a string mate");
      continue;
        case (PREPARE_ID_NEGETIVE):
      printf("ID cannot be negetive");
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
