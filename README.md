# MPLX - Multi-Language Programming Language eXperiment

Современный язык программирования с многоязычной экосистемой разработки.

## 🏗️ Архитектура

```
mplx/
├── Application/          # Слой приложения
│   ├── mplx-compiler/   # Компилятор MPLX
│   └── mplx-vm/         # Виртуальная машина
├── Domain/              # Доменный слой
│   └── mplx-lang/       # Языковые конструкции
├── Infrastructure/      # Инфраструктурный слой
│   ├── mplx-capi/       # C API для интеграции
│   ├── mplx-net/        # Сетевой модуль
│   ├── mplx-pkg/        # Пакетный менеджер
│   └── Mplx.DotNet/     # .NET wrapper
├── Presentation/        # Слой представления
│   ├── tools/           # CLI инструменты
│   ├── Mplx.Lsp/        # TypeScript LSP сервер
│   ├── vscode-mplx/     # VS Code extension
│   └── Mplx.DotNet.Sample/ # .NET пример
├── scripts/             # Скрипты сборки и настройки
└── build/               # Папка сборки
```

##  Быстрый старт

### 1. Настройка окружения
```powershell
# Установка зависимостей и сборка
.\scripts\bootstrap-mplx.ps1
```

### 2. Добавление компонентов
```powershell
# Сетевой модуль, пакетник, LSP
.\scripts\extend-mplx.ps1
```

### 3. Сборка DLL для .NET
```powershell
# Создание нативной DLL
.\scripts\build-simple-dll.ps1
```

## 🛠️ Компоненты

### C++ Core
- **Lexer/Parser** - Лексический и синтаксический анализ
- **Compiler** - Компиляция в байткод
- **VM** - Виртуальная машина для выполнения

### CLI Tools
```bash
# Проверка синтаксиса
mplx --check file.mplx

# Извлечение символов
mplx --symbols file.mplx

# Выполнение
mplx --run file.mplx
```

### .NET Integration
```csharp
// Использование из .NET
var result = MplxRuntime.RunFromSource(source, "main");
var diagnostics = MplxRuntime.CheckSource(source);
```

### TypeScript LSP
- **Language Server** - Анализ кода
- **VS Code Extension** - IDE поддержка

##  Структура проекта

| Слой | Описание | Технологии |
|------|----------|------------|
| **Domain** | Языковые конструкции | C++20 |
| **Application** | Компилятор и VM | C++20 |
| **Infrastructure** | Интеграции | C++, .NET, TypeScript |
| **Presentation** | UI и CLI | C++, TypeScript, .NET |

##  Демонстрация

Проект готов для демонстрации на собеседовании:
-  Многоязычная разработка
-  Clean Architecture
-  Современные технологии
-  Рабочие демо

##  Примеры

Смотрите `Presentation/examples/` для примеров кода на MPLX.