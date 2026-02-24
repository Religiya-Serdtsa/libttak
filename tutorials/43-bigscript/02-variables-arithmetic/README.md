# 02 - Variables and Arithmetic

BigScript allows you to define local variables and perform standard arithmetic operations.

## Variables

Variables are declared using the `let` keyword. BigScript uses lexical scoping within blocks.

```rust
let x = 10;
let y = x + 5;
```

## Arithmetic Operators

BigScript supports the following operators for integers:
- `+` : Addition
- `-` : Subtraction
- `*` : Multiplication
- `/` : Division (integer division for BigInt)
- `%` : Modulo

Note: All integers in BigScript are arbitrary-precision integers (BigInt).

## Running the Example

```bash
gcc main.c -o arithmetic -lttak
./arithmetic
```

Result:
- `z = (10 + 20) * 2 - 5 = 55`
- `quotient = 55 / 3 = 18`
- `remainder = 55 % 3 = 1`
- `return 18 + 1 = 19`
