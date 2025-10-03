# C Compiler and Execution System for DanOS

## Overview

DanOS now includes a simple C compiler and execution system that allows you to write, compile, and run C programs directly on the operating system.

## Features

- **Bytecode Compiler**: Compiles a subset of C to bytecode
- **Virtual Machine**: Executes compiled programs in a safe environment
- **File System Integration**: Programs are stored as executable files on the FAT32 filesystem
- **Simple System Calls**: Programs can print output and interact with the OS

## Supported C Features

The compiler now supports a comprehensive subset of C:

### Data Types
- `int` - 32-bit signed integers
- `char` - 8-bit characters
- Pointers (`int*`, `char*`) - basic pointer support

### Variables
- Variable declarations: `int x;` or `int x = 5;`
- Variable assignments: `x = 10;`
- Expressions with variables: `x = y + 5;`

### Operators
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `<`, `>`, `<=`, `>=`, `==`, `!=`
- Logical: `&&`, `||`, `!`

### Control Flow
- **If statements**: 
  ```c
  if (x > 5) {
      printf("x is greater than 5\n");
  } else {
      printf("x is 5 or less\n");
  }
  ```

- **While loops**:
  ```c
  int i = 0;
  while (i < 10) {
      printf("Loop iteration\n");
      i = i + 1;
  }
  ```

- **For loops**:
  ```c
  int i;
  for (i = 0; i < 10; i = i + 1) {
      printf("Counting...\n");
  }
  ```

### Functions
- **main() function**: Code can be wrapped in `int main() { ... }`
- Built-in functions:
  - `printf("string")` - print string with escape sequences (\n, \t, etc.)

### Comments
- Single-line comments: `// comment`
- Multi-line comments: `/* comment */`

## Commands

### compile - Compile C source code

```
compile source.c output.exe
```

Compiles a C source file into an executable bytecode file.

**Example:**
```
compile hello.c hello.exe
```

### exec - Execute a compiled program

```
exec program.exe
```

Loads and executes a compiled program.

**Example:**
```
exec hello.exe
```

## Creating Your First Program

### Method 1: Without main() function

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
