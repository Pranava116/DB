ERROR FIX EXPLANATION
=====================

Bug Location: read_exit.c, prepare_insert function (around line 208)

THE ERROR:
----------
In the prepare_insert function, the first call to strtok() was incorrectly using NULL 
instead of the input buffer:

  INCORRECT (original):
    char* id_string = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

  CORRECT (fixed):
    char* id_string = strtok(input_buffer->buffer, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

WHY IT CAUSED A CRASH:
-----------------------
- strtok() maintains internal state between calls
- The FIRST call to strtok() MUST receive the string to parse
- Subsequent calls should pass NULL to continue parsing the same string
- Passing NULL as the first argument initially caused undefined behavior
- This caused the program to crash (segmentation fault) when trying to parse "insert" command

OTHER FIXES MADE:
-----------------
1. Removed unused variable 'keywork' in prepare_insert
2. Changed print_row return type from void* to void
3. Removed unused parameter 'staatement' from execute_select
4. Cleaned up duplicate code in db_close function
