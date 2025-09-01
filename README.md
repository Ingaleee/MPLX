# 🚀 MPLX - Modern Programming Language eXperiment

**A complete language toolchain from scratch: Lexer → Parser → Compiler → VM + IDE Integration**

## 🎯 What Makes This Special

This project demonstrates **full-stack language engineering** - from core compiler technology to developer tooling:

- **🔧 C++20 Language Core**: Custom lexer, recursive descent parser, bytecode compiler, stack-based VM
- **🌐 TypeScript LSP Server**: Language Server Protocol implementation for VS Code integration  
- **⚡ Real-time Diagnostics**: JSON-based error reporting with line/column precision
- **🏗️ Clean Architecture**: Domain/Application/Infrastructure separation following DDD principles
- **🔗 Multi-language Interop**: .NET P/Invoke wrapper for C# integration
- **📦 Complete Toolchain**: Package manager, benchmarks, networking, ORM-lite

## 🏛️ Architecture Overview

```
┌─ Presentation Layer ─────────────────────────────────┐
│  • VS Code Extension     • CLI Tools    • Benchmarks │
├─ Application Layer ──────────────────────────────────┤  
│  • Compiler Service     • VM Runtime    • LSP Server │
├─ Domain Layer ───────────────────────────────────────┤
│  • Language Grammar    • AST Nodes     • Token Types │
├─ Infrastructure Layer ───────────────────────────────┤
│  • File I/O    • Networking    • Package Manager     │
└───────────────────────────────────────────────────────┘
```

## 🚀 Quick Demo

### 1. Basic Language Features
```rust
// hello.mplx - Supports functions, variables, arithmetic, control flow
fn main() -> i32 {
  let x = 1 + 2 * 3;
  if (x > 5) {
    x = x - 1;
  } else {
    x = x + 1;
  }
  return x;
}
```

### 2. CLI Tools in Action
```bash
# Parse and check syntax with detailed diagnostics
./mplx --check examples/hello.mplx
# Output: {"diagnostics": []}

# Extract symbols for IDE features  
./mplx --symbols examples/hello.mplx
# Output: {"functions": [{"name": "main", "arity": 0}]}

# Compile and execute
./mplx --run examples/hello.mplx
# Output: Result: 6
```

### 3. VS Code Integration
- **Syntax Highlighting**: Custom grammar for .mplx files
- **Error Squiggles**: Real-time parsing errors with precise locations
- **Hover Information**: Function signatures and documentation
- **Go to Definition**: Navigate between function declarations

## 🛠️ Technical Highlights

### Language Implementation
- **Lexical Analysis**: Hand-written lexer with support for keywords, operators, literals
- **Parsing**: Recursive descent parser generating Abstract Syntax Trees
- **Code Generation**: Bytecode compiler with constant folding and dead code elimination  
- **Runtime**: Stack-based virtual machine with call frames and local variables

### Modern DevOps Practices
- **CMake Build System**: Cross-platform with presets and toolchain files
- **Clean Git History**: Gitflow branching with semantic commits and versioned releases
- **Automated Testing**: Unit tests for lexer, parser, compiler, and VM components
- **Performance Monitoring**: Microbenchmarks for compilation and execution speed

### Integration Capabilities
- **Language Server Protocol**: VS Code extension with real-time diagnostics
- **Cross-Language Bindings**: C API with .NET P/Invoke wrapper
- **Network Protocol**: Binary framed TCP for distributed computing
- **Package Management**: Simple dependency resolution and locking

## 📊 What This Demonstrates

### Systems Programming Expertise
- Memory management and resource safety in C++20
- Binary protocol design and network programming
- Virtual machine architecture and bytecode optimization
- Cross-platform build system configuration

### Language Design Skills  
- Formal grammar specification and implementation
- Abstract syntax tree design and traversal
- Type system foundations and semantic analysis
- Error recovery and diagnostic reporting

### Modern Development Practices
- Clean architecture with dependency inversion
- Test-driven development with comprehensive coverage
- Continuous integration and automated builds
- Documentation-driven design and API contracts

### Full-Stack Integration
- Browser-based development environment (VS Code)
- Language server implementation following LSP specification  
- Multi-language interoperability (C++, TypeScript, C#)
- Package ecosystem design and dependency management

## 🎯 Business Value

This project showcases the ability to:

- **Architect Complex Systems**: Design and implement a complete language toolchain
- **Learn Rapidly**: Master new domains (compiler theory, language servers, protocols)
- **Think Systematically**: Break down large problems into manageable components
- **Deliver Quality**: Write maintainable, well-tested, production-ready code
- **Innovate**: Create novel solutions to challenging technical problems

## 🚧 Extensibility

The modular architecture enables easy extension:

- **New Language Features**: Add syntax for classes, generics, async/await
- **Target Platforms**: Generate LLVM IR, WebAssembly, or native machine code  
- **IDE Support**: Extend to JetBrains, Emacs, Vim through LSP protocol
- **Runtime Optimizations**: JIT compilation, garbage collection, profiling
- **Standard Library**: Rich ecosystem of built-in functions and data structures

---

*This project represents a deep dive into language implementation, demonstrating both theoretical knowledge and practical engineering skills across the entire development stack.*