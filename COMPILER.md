# DanOS C Compiler - User Guide

This document provides comprehensive examples of how to use the full-featured C compiler implemented in DanOS.

## Table of Contents
- [Basic Usage](#basic-usage)
- [Data Types](#data-types)
- [Variables and Arrays](#variables-and-arrays)
- [Pointers](#pointers)
- [Structs](#structs)
- [Functions](#functions)
- [Control Flow](#control-flow)
- [Preprocessor](#preprocessor)
- [Header Files](#header-files)
- [Complete Programs](#complete-programs)

## Basic Usage

To compile a C program in DanOS:
```bash
compile myprogram.c myprogram.exe
```

## Data Types

The compiler supports the following data types:

### Basic Types
```c
// Integer (32-bit)
int number = 42;
int negative = -100;

// Character (8-bit)
char letter = 'A';
char newline = '\n';
char tab = '\t';

// Void (for functions)
void my_function() {
    printf("Hello World\n");
}
```

### Type Casting and Sizeof
```c
int main() {
    int x = 65;
    char c = (char)x;  // Cast integer to character
    
    int size_of_int = sizeof(int);     // Returns 4
    int size_of_char = sizeof(char);   // Returns 1
    
    printf("Integer: %d, Character: %c\n", x, c);
    return 0;
}
```

## Variables and Arrays

### Variable Declarations
```c
int main() {
    // Simple variables
    int age = 25;
    char grade = 'A';
    
    // Array declarations
    int numbers[10];        // Array of 10 integers
    char name[32];          // Array of 32 characters (string)
    int matrix[5][5];       // 2D array (treated as 1D internally)
    
    // Array initialization
    numbers[0] = 100;
    numbers[1] = 200;
    name[0] = 'H';
    name[1] = 'i';
    name[2] = '\0';
    
    printf("Age: %d, Grade: %c\n", age, grade);
    printf("First number: %d\n", numbers[0]);
    
    return 0;
}
```

### String Literals
```c
int main() {
    // String literals
    printf("Hello, World!\n");
    printf("Escape sequences: \n\t\\\"");
    
    // String assignment (arrays)
    char greeting[20];
    // Note: String copying would need to be implemented manually
    
    return 0;
}
```

## Pointers

The compiler supports pointers up to double indirection (**).

### Basic Pointers
```c
int main() {
    int value = 42;
    int* ptr = &value;          // Pointer to integer
    int result = *ptr;          // Dereference pointer
    
    printf("Value: %d\n", value);
    printf("Dereferenced: %d\n", result);
    
    // Modify through pointer
    *ptr = 100;
    printf("Modified value: %d\n", value);
    
    return 0;
}
```

### Pointer to Pointer
```c
int main() {
    int value = 42;
    int* ptr = &value;
    int** ptr_to_ptr = &ptr;    // Pointer to pointer
    
    printf("Value: %d\n", value);
    printf("Through ptr: %d\n", *ptr);
    printf("Through ptr_to_ptr: %d\n", **ptr_to_ptr);
    
    // Modify through double pointer
    **ptr_to_ptr = 200;
    printf("Modified value: %d\n", value);
    
    return 0;
}
```

## Structs

### Struct Definition and Usage
```c
// Define a struct
struct Point {
    int x;
    int y;
};

struct Person {
    char name[32];
    int age;
    struct Point location;
};

int main() {
    // Declare struct variables
    struct Point p1;
    struct Person john;
    
    // Initialize struct members
    p1.x = 10;
    p1.y = 20;
    
    john.age = 30;
    john.location.x = 100;
    john.location.y = 200;
    
    printf("Point: (%d, %d)\n", p1.x, p1.y);
    printf("Person age: %d\n", john.age);
    
    return 0;
}
```

## Functions

### Function Declaration and Definition
```c
// Function declarations (prototypes)
int add(int a, int b);
void print_number(int n);

// Function definitions
int add(int a, int b) {
    return a + b;
}

void print_number(int n) {
    printf("Number: %d\n", n);
}

int main() {
    int x = 5, y = 10;
    int sum = add(x, y);
    
    print_number(sum);
    
    return 0;
}
```

## Control Flow

### Enhanced For Loops
```c
int main() {
    // For loop with initialization, condition, increment
    for (int i = 0; i < 10; i++) {
        printf("i = %d\n", i);
    }
    
    // Nested for loops
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            printf("(%d, %d) ", i, j);
        }
        printf("\n");
    }
    
    return 0;
}
```

### Do-While Loops
```c
int main() {
    int x = 0;
    do {
        printf("X: %d\n", x);
        x++;
    } while (x < 3);
    
    return 0;
}
```

## Preprocessor

### Macros and Defines
```c
#define PI 3.14159
#define MAX_SIZE 100

int main() {
    int buffer[MAX_SIZE];
    printf("Buffer size: %d\n", MAX_SIZE);
    
    return 0;
}
```

### Header Files
```c
#include "math_utils.h"

int main() {
    int result = add(5, 3);
    printf("Result: %d\n", result);
    return 0;
}
```

## Commands

### compile - Compile C source code
```bash
compile source.c output.exe
```

### exec - Execute a compiled program
```bash
exec program.exe
```

## Complete Example Programs

### Calculator Program
```c
int add(int a, int b) { return a + b; }
int subtract(int a, int b) { return a - b; }
int multiply(int a, int b) { return a * b; }

int main() {
    int x = 20, y = 10;
    
    printf("%d + %d = %d\n", x, y, add(x, y));
    printf("%d - %d = %d\n", x, y, subtract(x, y));
    printf("%d * %d = %d\n", x, y, multiply(x, y));
    
    return 0;
}
```

### Array Processing
```c
void print_array(int arr[], int size) {
    printf("Array: [");
    for (int i = 0; i < size; i++) {
        printf("%d", arr[i]);
        if (i < size - 1) printf(", ");
    }
    printf("]\n");
}

int main() {
    int numbers[5] = {10, 20, 30, 40, 50};
    
    print_array(numbers, 5);
    
    // Modify array
    for (int i = 0; i < 5; i++) {
        numbers[i] = numbers[i] * 2;
    }
    
    print_array(numbers, 5);
    
    return 0;
}
```

## Advanced Features

The new compiler supports:
- **Structs**: Complete struct support with member access
- **Function pointers**: Assign and call functions through pointers  
- **Multi-level pointers**: Up to double pointers (**)
- **Complex expressions**: Full operator precedence
- **Scoped variables**: Block-level variable scoping
- **Header files**: #include directive support
- **Preprocessor**: #define and #ifdef directives
- **Arrays**: Array declarations and subscript access
- **Type casting**: Convert between compatible types
- **Return statements**: Functions can return values

## Getting Started

1. Create a C source file using the text editor:
   ```
   wr hello.c
   ```

2. Type your C code:
   ```c
   printf("Hello, DanOS!\n");
   printf("Welcome to C programming!\n");
   ```

3. Save the file (press Ctrl)

4. Compile the program:
   ```
   compile hello.c hello.exe
   ```

5. Run the program:
   ```
   exec hello.exe
   ```

### Method 2: With main() function

You can also wrap your code in a proper main() function:

1. Create source file:
   ```
   wr hello.c
   ```

2. Type:
   ```c
   int main()
   {
       printf("Hello from main!\n");
       printf("This works too!\n");
   }
   ```

3. Compile and run:
   ```
   compile hello.c hello.exe
   exec hello.exe
   ```

## Example Programs

### Hello World

**File: hello.c**
```c
printf("Hello, World!\n");
```

Or with main():
```c
int main() {
    printf("Hello, World!\n");
}
```

**Compile and run:**
```
compile hello.c hello.exe
exec hello.exe
```

### Variables and Math

**File: math.c**
```c
int main() {
    int x = 10;
    int y = 20;
    int sum = x + y;
    
    printf("Math calculation\n");
    printf("Result computed\n");
}
```

### Loops

**File: loop.c**
```c
int main() {
    int i;
    for (i = 0; i < 5; i = i + 1) {
        printf("Loop iteration\n");
    }
    printf("Done!\n");
}
```

### Conditionals

**File: test.c**
```c
int main() {
    int x = 15;
    
    if (x > 10) {
        printf("x is greater than 10\n");
    } else {
        printf("x is 10 or less\n");
    }
}
```

### While Loop

**File: count.c**
```c
int main() {
    int counter = 0;
    
    while (counter < 3) {
        printf("Counting...\n");
        counter = counter + 1;
    }
    printf("Done counting!\n");
}
```

## Executable Format

DanOS uses a custom executable format called DANX:

- **Magic Number**: 0x44414E58 ("DANX")
- **Sections**: Code section (bytecode) and Data section (strings)
- **Entry Point**: Offset to start of execution

## System Architecture

```
Source Code (.c)
    ↓
Compiler (compiler.c)
    ↓
Bytecode + Data
    ↓
Executable File (.exe)
    ↓
Loader (exec.c)
    ↓
Virtual Machine
    ↓
Program Output
```

## Technical Details

### Memory Layout

- **User Code**: 0x400000 (4MB)
- **User Data**: 0x500000 (5MB)
- **User Stack**: 0x600000 (6MB)
- **Temp Buffer**: 0x700000 (7MB)
- **Source Buffer**: 0x800000 (8MB)

### Bytecode Instructions

The VM supports the following operations:
- `OP_PUSH` - Push constant to stack
- `OP_POP` - Pop value from stack
- `OP_ADD/SUB/MUL/DIV` - Arithmetic operations
- `OP_PRINT` - Print numeric value
- `OP_PRINT_STR` - Print string
- `OP_HALT` - Stop execution

### System Calls

Programs can make system calls (future enhancement):
- `SYSCALL_PRINT` - Print string
- `SYSCALL_EXIT` - Exit program
- `SYSCALL_OPEN` - Open file
- `SYSCALL_READ` - Read file
- `SYSCALL_WRITE` - Write file
- `SYSCALL_CLOSE` - Close file

## Limitations

Current limitations:
- Only `printf()` with string literals is supported
- No function definitions
- No variables (yet)
- No control flow statements (yet)
- Maximum source file size: 4KB
- Maximum executable size: depends on available memory

## Future Enhancements

Planned features:
- Support for variables and expressions
- Function definitions and calls
- Control flow (if/else, while, for)
- More complete C standard library
- Optimization passes
- Better error messages
- Debugging support

## Troubleshooting

### "Compilation failed"
- Check your C syntax
- Ensure you're using supported features only
- Check that the source file exists

### "Error: Invalid executable format"
- The file is not a valid DANX executable
- Re-compile the source file

### "Error: Could not open file"
- Check that the file exists using `ls`
- Verify the filename spelling

## Development

The compiler and execution system consists of:

- `src/kernel/includes/compiler.h` - Compiler interface
- `src/kernel/includes/exec.h` - Execution interface
- `src/kernel/drivers/compiler.c` - Compiler implementation
- `src/kernel/drivers/exec.c` - VM and loader
- `src/kernel/commands/commands.c` - User commands

To extend the compiler:
1. Add new opcodes to the VM in `exec.h`
2. Implement parsing logic in `compiler.c`
3. Add execution logic in `exec.c`
