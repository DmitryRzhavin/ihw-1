#ifndef CONFIG_H
#define CONFIG_H

#include "model.h"

/*
 * Загружает конфигурацию из файла.
 * Возвращает 0 при успехе и ненулевое значение при ошибке.
 */
int config_load_file(const char *path, Config *config);

/*
 * Запрашивает базовые параметры у пользователя.
 * Возвращает 0 при успехе и ненулевое значение при ошибке.
 */
int config_read_interactive(Config *config);

/* Проверяет допустимость всех параметров конфигурации. */
int config_validate(const Config *config);

#endif
