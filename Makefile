CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -O2 -g -fPIC -pthread
LDFLAGS = -ldl -lpthread
TEST_CFLAGS = $(CFLAGS) -I.
TEST_LDFLAGS = $(LDFLAGS) -L.

# Исходные файлы
MAIN_SRC = main.c rpc.c rpc_commands.c module_loader.c module_error.c
MOD_SRC = mod.c

# Объектные файлы
MAIN_OBJ = $(MAIN_SRC:.c=.o)
MOD_OBJ = $(MOD_SRC:.c=.o)
LIB_OBJ = module_loader.o module_error.o

# Целевые файлы
BIN_TARGET = kmodlike
MOD_TARGET = mod.so
LIB_TARGET = libmodule.so

# Тестовые файлы
TEST_SRC = tests/test_module_loader.c tests/test_crash_recovery.c tests/test_stress_concurrent.c tests/test_stress_rpc.c
TEST_FIXTURES = tests/fixtures/test_mod_good.c tests/fixtures/test_mod_no_init.c tests/fixtures/test_mod_bad_init.c tests/fixtures/test_mod_crash.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_FIXTURE_OBJ = $(TEST_FIXTURES:.c=.o)
TEST_FIXTURE_SO = $(TEST_FIXTURES:.c=.so)
TEST_BIN = tests/test_module_loader
TEST_INT_BIN = tests/test_crash_recovery
TEST_STRESS_BIN = tests/test_stress_concurrent
TEST_STRESS_RPC_BIN = tests/test_stress_rpc

.PHONY: all bin mod lib clean test test-unit test-integration test-stress test-stress-rpc

# Сборка всего
all: bin mod

# Сборка основной программы
bin: $(BIN_TARGET)

# Сборка модуля
mod: $(MOD_TARGET)

# Сборка библиотеки
lib: $(LIB_TARGET)

# Основная программа
$(BIN_TARGET): $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Модуль (shared library)
$(MOD_TARGET): $(MOD_OBJ)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LDFLAGS)

# Библиотека module_loader
$(LIB_TARGET): $(LIB_OBJ)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LDFLAGS)

# Компиляция объектных файлов
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Тестовые модули
tests/fixtures/%.so: tests/fixtures/%.c
	$(CC) $(CFLAGS) -shared -o $@ $< $(LDFLAGS)

# Unit тесты
$(TEST_BIN): tests/test_module_loader.o $(LIB_OBJ) $(TEST_FIXTURE_SO)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_module_loader.o $(LIB_OBJ) $(TEST_LDFLAGS)

# Интеграционные тесты
$(TEST_INT_BIN): tests/test_crash_recovery.o $(LIB_OBJ) $(TEST_FIXTURE_SO)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_crash_recovery.o $(LIB_OBJ) $(TEST_LDFLAGS)

# Стресс-тесты
$(TEST_STRESS_BIN): tests/test_stress_concurrent.o $(LIB_OBJ) $(TEST_FIXTURE_SO)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_stress_concurrent.o $(LIB_OBJ) $(TEST_LDFLAGS)

# RPC стресс-тесты
$(TEST_STRESS_RPC_BIN): tests/test_stress_rpc.o $(LIB_OBJ) rpc.o rpc_commands.o $(TEST_FIXTURE_SO)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_stress_rpc.o $(LIB_OBJ) rpc.o rpc_commands.o $(TEST_LDFLAGS)

# Запуск unit тестов
test-unit: $(TEST_BIN)
	$(TEST_BIN)

# Запуск интеграционных тестов
test-integration: $(TEST_INT_BIN)
	$(TEST_INT_BIN)

# Запуск стресс-тестов
test-stress: $(TEST_STRESS_BIN)
	$(TEST_STRESS_BIN)

# Запуск RPC стресс-тестов
test-stress-rpc: $(TEST_STRESS_RPC_BIN)
	$(TEST_STRESS_RPC_BIN)

# Все тесты
test: test-unit test-integration test-stress test-stress-rpc

# Очистка
clean:
	rm -f $(MAIN_OBJ) $(MOD_OBJ) $(LIB_OBJ) $(TEST_OBJ) $(TEST_FIXTURE_OBJ) $(TEST_FIXTURE_SO)
	rm -f $(BIN_TARGET) $(MOD_TARGET) $(LIB_TARGET) $(TEST_BIN) $(TEST_INT_BIN) $(TEST_STRESS_BIN) $(TEST_STRESS_RPC_BIN)

