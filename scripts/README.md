# MPLX Scripts

Эта папка содержит все скрипты для сборки и настройки проекта MPLX.

## Скрипты сборки

### `bootstrap-mplx.ps1`
Основной скрипт для первоначальной настройки проекта:
- Устанавливает vcpkg
- Настраивает переменные окружения
- Собирает проект с CMake

### `extend-mplx.ps1`
Добавляет дополнительные компоненты:
- Сетевой модуль (asio)
- Пакетник (mplx-pkg)
- Исправления компилятора
- Улучшения LSP

### `build-msvc-dll.ps1`
Создает нативную DLL с MSVC для .NET интеграции

### `build-simple-dll.ps1`
Создает простую DLL для демонстрации .NET интеграции

## Скрипты интеграции

### `add-dotnet-wrapper.ps1`
Добавляет .NET wrapper для MPLX

### `step2-apply.ps1`, `step3-apply.ps1`
Дополнительные шаги настройки

## Использование

```powershell
# Основная настройка
.\scripts\bootstrap-mplx.ps1

# Добавление компонентов
.\scripts\extend-mplx.ps1

# Сборка DLL для .NET
.\scripts\build-simple-dll.ps1
```
