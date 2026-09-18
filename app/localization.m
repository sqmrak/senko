#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import "app_common.h"

NSString * const SenkoLanguageDidChangeNotification = @"SenkoLanguageDidChangeNotification";

static NSMutableDictionary *gEnglishToRussian;
static NSMutableDictionary *gRussianToEnglish;
static NSMutableDictionary *gEnglishToChinese;
static BOOL gLocalizationInstalled = NO;

static char kSenkoRawText;
static char kSenkoRawButtonTitles;
static char kSenkoRawPlaceholder;
static char kSenkoRawControllerTitle;
static char kSenkoRawNavigationTitle;
static char kSenkoRawBarTitle;

static void SenkoAddTranslation(NSString *english, NSString *russian) {
    if (![english length] || ![russian length]) return;
    [gEnglishToRussian setObject:russian forKey:english];
    [gRussianToEnglish setObject:english forKey:russian];
}

static void SenkoAddChineseTranslation(NSString *english, NSString *chinese) {
    if (![english length] || ![chinese length]) return;
    [gEnglishToChinese setObject:chinese forKey:english];
}

static void SenkoBuildTranslations(void) {
    if (gEnglishToRussian) return;
    gEnglishToRussian = [[NSMutableDictionary alloc] init];
    gRussianToEnglish = [[NSMutableDictionary alloc] init];
    gEnglishToChinese = [[NSMutableDictionary alloc] init];

    SenkoAddTranslation(@"About", @"О приложении");
    SenkoAddTranslation(@"PROTOCOL", @"ПРОТОКОЛ");
    SenkoAddTranslation(@"PASSWORD", @"ПАРОЛЬ");
    SenkoAddTranslation(@"CIPHER", @"ШИФР");
    SenkoAddTranslation(@"Shadowsocks", @"Shadowsocks");
    SenkoAddTranslation(@"Trojan", @"Trojan");
    SenkoAddTranslation(@"Add", @"Добавить");
    SenkoAddTranslation(@"Add server", @"Добавить сервер");
    SenkoAddTranslation(@"Allow insecure", @"Разрешить небезопасное");
    SenkoAddTranslation(@"All", @"Все");
    SenkoAddTranslation(@"all", @"все");
    SenkoAddTranslation(@"Almost done", @"Почти готово");
    SenkoAddTranslation(@"AmneziaWG", @"AmneziaWG");
    SenkoAddTranslation(@"Apply anyway", @"Всё равно применить");
    SenkoAddTranslation(@"Appearance", @"Оформление");
    SenkoAddTranslation(@"Cancel", @"Отмена");
    SenkoAddTranslation(@"Check ping", @"Проверить профиль");
    SenkoAddTranslation(@"Check", @"Проверить");
    SenkoAddTranslation(@"Check servers", @"Проверить серверы");
    SenkoAddTranslation(@"Checking package", @"Проверка пакета");
    SenkoAddTranslation(@"Close", @"Закрыть");
    SenkoAddTranslation(@"Complete", @"Готово");
    SenkoAddTranslation(@"connected", @"подключено");
    SenkoAddTranslation(@"connecting", @"подключение");
    SenkoAddTranslation(@"connecting...", @"подключение...");
    SenkoAddTranslation(@"Connection failed", @"Соединение не установлено");
    SenkoAddTranslation(@"connection failed", @"соединение не установлено");
    SenkoAddTranslation(@"Custom", @"Пользовательские");
    SenkoAddTranslation(@"DAEMON", @"ДЕМОН");
    SenkoAddTranslation(@"GENERAL", @"ОБЩИЕ");
    SenkoAddTranslation(@"CONNECTION", @"ПОДКЛЮЧЕНИЕ");
    SenkoAddTranslation(@"AUTOMATION", @"АВТОМАТИКА");
    SenkoAddTranslation(@"Connect at startup", @"Подключаться при старте");
    SenkoAddTranslation(@"Dial the selected server after a reboot",
                        @"Поднимать выбранный сервер после перезагрузки");
    SenkoAddTranslation(@"Reconnect automatically", @"Переподключаться автоматически");
    SenkoAddTranslation(@"After a drop or a change of network",
                        @"После обрыва или смены сети");
    SenkoAddTranslation(@"Try another server", @"Пробовать другой сервер");
    SenkoAddTranslation(@"Only inside the same section, fastest first",
                        @"Только внутри своего раздела, сначала быстрые");
    SenkoAddTranslation(@"Update subscriptions", @"Обновлять подписки");
    SenkoAddTranslation(@"Off", @"Выкл");
    SenkoAddTranslation(@"Every %d h", @"Каждые %d ч");
    SenkoAddTranslation(@"The daemon runs these on its own, with the app closed.",
                        @"Демон выполняет это сам, при закрытом приложении.");
    SenkoAddTranslation(@"The daemon is not answering, so these cannot be read or changed.",
                        @"Демон не отвечает, прочитать и изменить их нельзя.");
    SenkoAddTranslation(@"Daemon is unreachable", @"Демон не отвечает");
    SenkoAddTranslation(@"ROUTING", @"МАРШРУТИЗАЦИЯ");
    SenkoAddTranslation(@"DEVELOPER", @"РАЗРАБОТЧИКУ");
    SenkoAddTranslation(@"Developer section", @"Раздел разработчика");
    SenkoAddTranslation(@"It is now in settings, under the app section. Five taps on its heading hide it again.",
                        @"Он появился в настройках, под разделом приложения. Пять тапов по его заголовку убирают его обратно.");
    SenkoAddTranslation(@"Five taps on this heading hide the section again.",
                        @"Пять тапов по этому заголовку убирают раздел.");
    SenkoAddTranslation(@"%d more", @"ещё %d");
    SenkoAddTranslation(@"What was chosen", @"Что выбралось");
    SenkoAddTranslation(@"Copy", @"Копировать");
    SenkoAddTranslation(@"The report is on the clipboard.", @"Отчёт скопирован в буфер.");
    SenkoAddTranslation(@"Asking the daemon...", @"Спрашиваю демона...");
    SenkoAddTranslation(@"Tunnel state", @"Состояние туннеля");
    SenkoAddTranslation(@"Connected for", @"На связи");
    SenkoAddTranslation(@"Redial", @"Перенабор");
    SenkoAddTranslation(@"Egress interface", @"Исходящий интерфейс");
    SenkoAddTranslation(@"Egress address", @"Исходящий адрес");
    SenkoAddTranslation(@"Catalog", @"Каталог");
    SenkoAddTranslation(@"Selected server", @"Выбранный сервер");
    SenkoAddTranslation(@"Transport", @"Транспорт");
    SenkoAddTranslation(@"Flow", @"Flow");
    SenkoAddTranslation(@"Vision", @"Vision");
    SenkoAddTranslation(@"Daemon pid", @"PID демона");
    SenkoAddTranslation(@"Daemon uptime", @"Демон работает");
    SenkoAddTranslation(@"Jailbreak", @"Джейлбрейк");
    SenkoAddTranslation(@"Config file", @"Файл конфигурации");
    SenkoAddTranslation(@"iOS major", @"Версия iOS");
    SenkoAddTranslation(@"iOS version read from", @"Версия iOS определена по");
    SenkoAddTranslation(@"Backend in use", @"Активный бэкенд");
    SenkoAddTranslation(@"Go core eligible", @"Go-ядро подходит");
    SenkoAddTranslation(@"Applied on the next connect.", @"Применится при следующем подключении.");
    SenkoAddTranslation(@"Auto", @"Авто");
    SenkoAddTranslation(@"Backend, firewall, ports, DNS, rules", @"Бэкенд, файрвол, порты, DNS, правила");
    SenkoAddTranslation(@"Backend, pf variant, listeners, trace", @"Бэкенд, вариант pf, слушатели, трассировка");
    SenkoAddTranslation(@"Catalog and rules are kept.", @"Каталог и правила остаются.");
    SenkoAddTranslation(@"Catalog is kept.", @"Каталог остаётся.");
    SenkoAddTranslation(@"Checks", @"Проверки");
    SenkoAddTranslation(@"Clear", @"Очистить");
    SenkoAddTranslation(@"Console", @"Консоль");
    SenkoAddTranslation(@"Test fixtures", @"Тестовые данные");
    SenkoAddTranslation(@"Long names", @"Длинные названия");
    SenkoAddTranslation(@"Duplicates", @"Дубли");
    SenkoAddTranslation(@"Empty manual list", @"Пустой список ручных серверов");
    SenkoAddTranslation(@"Only manually added servers are removed.", @"Удаляются только добавленные вручную серверы.");
    SenkoAddTranslation(@"Done.", @"Готово.");
    SenkoAddTranslation(@"Copied in full.", @"Скопировано целиком.");
    SenkoAddTranslation(@"Crash and launch log", @"Лог падения и запуска");
    SenkoAddTranslation(@"Crash log, safe mode, device id, bundle", @"Лог падения, safe mode, id устройства, файл");
    SenkoAddTranslation(@"Delete all rules", @"Удалить все правила");
    SenkoAddTranslation(@"Failed launches", @"Неудачных запусков");
    SenkoAddTranslation(@"FIREWALL", @"ФАЙРВОЛ");
    SenkoAddTranslation(@"Flush DNS cache", @"Сбросить кэш DNS");
    SenkoAddTranslation(@"Force", @"Форс");
    SenkoAddTranslation(@"FORCE", @"ФОРС");
    SenkoAddTranslation(@"FPS overlay", @"Оверлей FPS");
    SenkoAddTranslation(@"LAUNCH", @"ЗАПУСК");
    SenkoAddTranslation(@"New device id", @"Новый id устройства");
    SenkoAddTranslation(@"no answer", @"нет ответа");
    SenkoAddTranslation(@"No servers in the catalog.", @"В каталоге нет серверов.");
    SenkoAddTranslation(@"Off from the next launch.", @"Выключен со следующего запуска.");
    SenkoAddTranslation(@"pf variant", @"Вариант pf");
    SenkoAddTranslation(@"Reset dynamic bypass", @"Сбросить динамический обход");
    SenkoAddTranslation(@"Reset settings", @"Сбросить настройки");
    SenkoAddTranslation(@"Run", @"Запустить");
    SenkoAddTranslation(@"Safe mode next launch", @"Safe mode при следующем запуске");
    SenkoAddTranslation(@"Session trace", @"Трассировка сессий");
    SenkoAddTranslation(@"Show ruleset", @"Показать правила");
    SenkoAddTranslation(@"SOCKS on 0.0.0.0", @"SOCKS на 0.0.0.0");
    SenkoAddTranslation(@"Staged probes and the firewall ruleset", @"Пробы по стадиям и правила файрвола");
    SenkoAddTranslation(@"STAGES", @"СТАДИИ");
    SenkoAddTranslation(@"Stock theme, no glass, no decor. Your theme stays on disk.",
                        @"Стоковая тема, без стекла и декора. Ваша тема останется на диске.");
    SenkoAddTranslation(@"Talk to the control socket without ssh", @"Говорить с управляющим сокетом без ssh");
    SenkoAddTranslation(@"The old id is gone for good.", @"Старый id пропадёт навсегда.");
    SenkoAddTranslation(@"Write bundle", @"Записать файл");
    SenkoAddTranslation(@"Export debug bundle", @"Экспорт debug bundle");
    SenkoAddTranslation(@"CHOSEN", @"ВЫБРАНО");
    SenkoAddTranslation(@"SERVER", @"СЕРВЕР");
    SenkoAddTranslation(@"LIVE", @"СЕЙЧАС");
    SenkoAddTranslation(@"DEVICE", @"УСТРОЙСТВО");
    SenkoAddTranslation(@"FORCED", @"ФОРСИРОВАНО");
    SenkoAddTranslation(@"PROCESSES", @"ПРОЦЕССЫ");
    SenkoAddTranslation(@"PATHS", @"ПУТИ");
    SenkoAddTranslation(@"Backend forced to", @"Бэкенд форсирован на");
    SenkoAddTranslation(@"pf variant forced to", @"Вариант pf форсирован на");
    SenkoAddTranslation(@"Free memory", @"Свободная память");
    SenkoAddTranslation(@"Senko resident", @"Senko занимает");
    SenkoAddTranslation(@"Device uptime", @"Устройство работает");
    SenkoAddTranslation(@"Device", @"Устройство");
    SenkoAddTranslation(@"Battery", @"Батарея");
    SenkoAddTranslation(@"Go core usable", @"Go-ядро подходит");
    SenkoAddTranslation(@"pf rejected", @"pf отверг");
    SenkoAddTranslation(@"Bypass evicted", @"Вытеснено из обхода");
    SenkoAddTranslation(@"iOS read from", @"Версия iOS из");
    SenkoAddTranslation(@"Top rule", @"Топ правило");
    SenkoAddTranslation(@"Rule 2", @"Правило 2");
    SenkoAddTranslation(@"Rule 3", @"Правило 3");
    SenkoAddTranslation(@"Rules", @"Правила");
    SenkoAddTranslation(@"Block response", @"Ответ на блок");
    SenkoAddTranslation(@"Developer", @"Разработчик");
    SenkoAddTranslation(@"What was chosen, checks, overrides and rescue",
                        @"Что выбралось, проверки, переключатели и аварийный раздел");
    SenkoAddTranslation(@"Rescue", @"Аварийный раздел");
    SenkoAddTranslation(@"Backend pinned to", @"Бэкенд закреплён на");
    SenkoAddTranslation(@"pf syntax pinned to", @"Вариант pf закреплён на");
    SenkoAddTranslation(@"Bypass evictions", @"Вытеснено из таблицы обхода");
    SenkoAddTranslation(@"SOCKS bound to", @"SOCKS слушает");
    SenkoAddTranslation(@"Blocked answers", @"Ответ на заблокированное");
    SenkoAddTranslation(@"DNS cache", @"Кэш DNS");
    SenkoAddTranslation(@"Busiest rule", @"Самое частое правило");
    SenkoAddTranslation(@"Second busiest rule", @"Второе по частоте правило");
    SenkoAddTranslation(@"Third busiest rule", @"Третье по частоте правило");
    SenkoAddTranslation(@"Rule hits", @"Срабатывания правил");
    SenkoAddTranslation(@"Device gating", @"Привязка к устройству");
    SenkoAddTranslation(@"Jailbreak root", @"Корень джейлбрейка");
    SenkoAddTranslation(@"Device id file", @"Файл id устройства");
    SenkoAddTranslation(@"System log", @"Системный лог");
    SenkoAddTranslation(@"Substrate directory", @"Каталог Substrate");
    SenkoAddTranslation(@"senko-kick", @"senko-kick");
    SenkoAddTranslation(@"CHECK", @"ПРОВЕРКА");
    SenkoAddTranslation(@"Server", @"Сервер");
    SenkoAddTranslation(@"Mode", @"Режим");
    SenkoAddTranslation(@"No server in the catalog", @"В каталоге нет серверов");
    SenkoAddTranslation(@"TCP to the node", @"TCP до узла");
    SenkoAddTranslation(@"Local proxy", @"Локальный прокси");
    SenkoAddTranslation(@"Active tunnel", @"Активный туннель");
    SenkoAddTranslation(@"Profile handshake", @"Рукопожатие профиля");
    SenkoAddTranslation(@"Running...", @"Идёт проверка...");
    SenkoAddTranslation(@"Result", @"Результат");
    SenkoAddTranslation(@"passed in %d ms", @"прошла за %d мс");
    SenkoAddTranslation(@"Generated firewall ruleset", @"Сгенерированные правила файрвола");
    SenkoAddTranslation(@"Firewall ruleset", @"Правила файрвола");
    SenkoAddTranslation(@"LISTENERS AND DNS", @"СЛУШАТЕЛИ И DNS");
    SenkoAddTranslation(@"SUBSCRIPTIONS", @"ПОДПИСКИ");
    SenkoAddTranslation(@"DIAGNOSTICS", @"ДИАГНОСТИКА");
    SenkoAddTranslation(@"Backend", @"Бэкенд");
    SenkoAddTranslation(@"Go core", @"Go-ядро");
    SenkoAddTranslation(@"C core", @"C-ядро");
    SenkoAddTranslation(@"Connect hook only", @"Только connect-хук");
    SenkoAddTranslation(@"Zero address", @"Нулевой адрес");
    SenkoAddTranslation(@"Flush the DNS cache", @"Сбросить кэш DNS");
    SenkoAddTranslation(@"Ignore device gating", @"Игнорировать привязку к устройству");
    SenkoAddTranslation(@"Flush", @"Сбросить");
    SenkoAddTranslation(@"Reset", @"Сбросить");
    SenkoAddTranslation(@"Done.", @"Готово.");
    SenkoAddTranslation(@"DEVICE ID", @"ID УСТРОЙСТВА");
    SenkoAddTranslation(@"REMOVE", @"УДАЛЕНИЕ");
    SenkoAddTranslation(@"Crash and launch report", @"Отчёт о падении и запуске");
    SenkoAddTranslation(@"nothing recorded", @"ничего не записано");
    SenkoAddTranslation(@"Leave safe mode", @"Выйти из safe mode");
    SenkoAddTranslation(@"Enter", @"Войти");
    SenkoAddTranslation(@"The next launch runs in safe mode.", @"Следующий запуск пройдёт в safe mode.");
    SenkoAddTranslation(@"Device id", @"ID устройства");
    SenkoAddTranslation(@"Copied to the clipboard.", @"Скопировано в буфер.");
    SenkoAddTranslation(@"Issue", @"Выдать");
    SenkoAddTranslation(@"Diagnostics bundle", @"Файл диагностики");
    SenkoAddTranslation(@"the daemon did not answer", @"демон не ответил");
    SenkoAddTranslation(@"unknown check type", @"неизвестный тип проверки");
    SenkoAddTranslation(@"Last backend error", @"Последняя ошибка бэкенда");
    SenkoAddTranslation(@"Firewall", @"Файрвол");
    SenkoAddTranslation(@"Accepted pf syntax", @"Принятый синтаксис pf");
    SenkoAddTranslation(@"Last pf rejection", @"Последний отказ pf");
    SenkoAddTranslation(@"Bypass table", @"Таблица обхода");
    SenkoAddTranslation(@"Redirect port", @"Порт перенаправления");
    SenkoAddTranslation(@"DNS port", @"Порт DNS");
    SenkoAddTranslation(@"Live connections", @"Живых соединений");
    SenkoAddTranslation(@"senkoawgd", @"senkoawgd");
    SenkoAddTranslation(@"TLS compatibility hook", @"Хук совместимости TLS");
    SenkoAddTranslation(@"Status bar hook", @"Хук строки состояния");
    SenkoAddTranslation(@"Routing rules", @"Правила маршрутизации");
    SenkoAddTranslation(@"Send a domain or a subnet direct, or block it",
                        @"Пустить домен или подсеть напрямую, либо заблокировать");
    SenkoAddTranslation(@"Upstream DNS", @"Внешний DNS");
    SenkoAddTranslation(@"Local DNS port", @"Локальный порт DNS");
    SenkoAddTranslation(@"SOCKS port", @"Порт SOCKS");
    SenkoAddTranslation(@"An IPv4 address, for example 1.1.1.1",
                        @"Адрес IPv4, например 1.1.1.1");
    SenkoAddTranslation(@"A port number between 1 and 65535",
                        @"Номер порта от 1 до 65535");
    SenkoAddTranslation(@"DNS settings apply the next time the tunnel comes up. The SOCKS port applies when the daemon restarts.",
                        @"Настройки DNS применяются при следующем подъёме туннеля. Порт SOCKS - при перезапуске демона.");
    SenkoAddTranslation(@"Reconnect attempts", @"Попыток переподключения");
    SenkoAddTranslation(@"Until it works", @"Пока не получится");
    SenkoAddTranslation(@"%d attempts", @"%d попыток");
    SenkoAddTranslation(@"Save", @"Сохранить");
    SenkoAddTranslation(@"Direct", @"Напрямую");
    SenkoAddTranslation(@"Block", @"Блокировать");
    SenkoAddTranslation(@"Through the tunnel", @"Через туннель");
    SenkoAddTranslation(@"Domain and subdomains", @"Домен и поддомены");
    SenkoAddTranslation(@"Keyword", @"Ключевое слово");
    SenkoAddTranslation(@"IP range", @"Диапазон адресов");
    SenkoAddTranslation(@"What should happen to the traffic?", @"Что делать с трафиком?");
    SenkoAddTranslation(@"What should it match?", @"По чему сопоставлять?");
    SenkoAddTranslation(@"For example example.com", @"Например example.com");
    SenkoAddTranslation(@"For example googlevideo", @"Например googlevideo");
    SenkoAddTranslation(@"For example 10.0.0.0/8", @"Например 10.0.0.0/8");
    SenkoAddTranslation(@"Value", @"Значение");
    SenkoAddTranslation(@"hits", @"срабатываний");
    SenkoAddTranslation(@"Reading the rules from the daemon...",
                        @"Читаю правила у демона...");
    SenkoAddTranslation(@"No rules: everything goes through the tunnel. Add one with the plus button.",
                        @"Правил нет: весь трафик идёт через туннель. Добавьте правило кнопкой «плюс».");
    SenkoAddTranslation(@"Block wins over direct, direct wins over the tunnel, whatever the order. On iOS 12 and later the tunnel core reads the real domain from the connection; below that the rule is matched when the name is resolved, so an address shared by several sites follows the first name that asked for it.",
                        @"Блокировка сильнее «напрямую», «напрямую» сильнее туннеля, порядок правил не важен. На iOS 12 и новее ядро туннеля видит настоящий домен соединения; ниже правило применяется в момент разрешения имени, поэтому адрес, общий для нескольких сайтов, идёт по первому запросившему имени.");
    SenkoAddTranslation(@"APP", @"ПРИЛОЖЕНИЕ");
    SenkoAddTranslation(@"Dark", @"Тёмная");
    SenkoAddTranslation(@"Disconnect first", @"Сначала отключитесь");
    SenkoAddTranslation(@"Done", @"Готово");
    SenkoAddTranslation(@"Edit", @"Изменить");
    SenkoAddTranslation(@"Edit details", @"Изменить параметры");
    SenkoAddTranslation(@"Edit selected server", @"Изменить выбранный сервер");
    SenkoAddTranslation(@"Edit server", @"Изменить сервер");
    SenkoAddTranslation(@"Encrypted subscription", @"Зашифрованная подписка");
    SenkoAddTranslation(@"English", @"Английский");
    SenkoAddTranslation(@"Error", @"Ошибка");
    SenkoAddTranslation(@"expired", @"истёк");
    SenkoAddTranslation(@"Failed", @"Ошибка");
    SenkoAddTranslation(@"Finishing", @"Завершение");
    SenkoAddTranslation(@"FINGERPRINT", @"ОТПЕЧАТОК");
    SenkoAddTranslation(@"FLOW", @"ПОТОК");
    SenkoAddTranslation(@"idle", @"ожидание");
    SenkoAddTranslation(@"Import file", @"Импорт файла");
    SenkoAddTranslation(@"Export configuration", @"Экспорт конфигурации");
    SenkoAddTranslation(@"Restore configuration", @"Восстановить конфигурацию");
    SenkoAddTranslation(@"Export backup", @"Экспорт резервной копии");
    SenkoAddTranslation(@"Restore backup", @"Восстановить резервную копию");
    SenkoAddTranslation(@"Save a config file to Documents", @"Сохранить файл конфигурации в Documents");
    SenkoAddTranslation(@"Validate, then replace configuration", @"Проверить и заменить конфигурацию");
    SenkoAddTranslation(@"Choose a Senko .deb package", @"Выбрать пакет Senko .deb");
    SenkoAddTranslation(@"Manually added profiles only", @"Только добавленные вручную профили");
    SenkoAddTranslation(@"validated import", @"импорт с проверкой");
    SenkoAddTranslation(@"Configuration backup", @"Резервная копия");
    SenkoAddTranslation(@"Not a senko backup", @"Это не резервная копия senko");
    SenkoAddTranslation(@"Replace configuration?", @"Заменить конфигурацию?");
    SenkoAddTranslation(@"The imported backup will replace all current servers and subscriptions.", @"Импортированная копия заменит все текущие серверы и подписки.");
    SenkoAddTranslation(@"Replace", @"Заменить");
    SenkoAddTranslation(@"Configuration restored", @"Конфигурация восстановлена");
    SenkoAddTranslation(@"Saved to Documents/senko-backup.senko", @"Сохранено в Documents/senko-backup.senko");
    SenkoAddTranslation(@"Subscription details", @"Данные подписки");
    SenkoAddTranslation(@"Used", @"Использовано");
    SenkoAddTranslation(@"Remaining", @"Осталось");
    SenkoAddTranslation(@"Limit", @"Лимит");
    SenkoAddTranslation(@"Uploaded", @"Отправлено");
    SenkoAddTranslation(@"Expires", @"Действует до");
    SenkoAddTranslation(@"Expired", @"Срок действия истёк");
    SenkoAddTranslation(@"Downloaded", @"Получено");
    SenkoAddTranslation(@"Description", @"Описание");
    SenkoAddTranslation(@"Contact support", @"Связаться с поддержкой");
    SenkoAddTranslation(@"Not provided", @"Нет данных");
    SenkoAddTranslation(@"Install failed", @"Ошибка установки");
    SenkoAddTranslation(@"Installing", @"Установка");
    SenkoAddTranslation(@"Light", @"Светлая");
    SenkoAddTranslation(@"Language", @"Язык");
    SenkoAddTranslation(@"Manual", @"Вручную");
    SenkoAddTranslation(@"NAME", @"НАЗВАНИЕ");
    SenkoAddTranslation(@"Name", @"Название");
    SenkoAddTranslation(@"No camera available", @"Камера недоступна");
    SenkoAddTranslation(@"Camera access could not be requested", @"Не удалось запросить доступ к камере");
    SenkoAddTranslation(@"Camera access is disabled\nEnable it in Settings > Privacy > Camera", @"Доступ к камере отключён\nРазрешите его в Настройки > Конфиденциальность > Камера");
    SenkoAddTranslation(@"No daemon logs available", @"Логи демона недоступны");
    SenkoAddTranslation(@"No app fault report", @"Отчётов о сбоях нет");
    SenkoAddTranslation(@"Loading logs...", @"Загрузка логов...");
    SenkoAddTranslation(@"reading content...", @"чтение содержимого...");
    SenkoAddTranslation(@"removing manual servers...", @"удаление серверов...");
    SenkoAddTranslation(@"manual servers removed", @"серверы удалены");
    SenkoAddTranslation(@"Add subscription", @"Добавить подписку");
    SenkoAddTranslation(@"Paste from clipboard", @"Вставить из буфера");
    SenkoAddTranslation(@"Paste", @"Вставить");
    SenkoAddTranslation(@"Safe mode", @"Безопасный режим");
    SenkoAddTranslation(@"senko-kick could not be waited for",
                        @"не удалось дождаться senko-kick");
    SenkoAddTranslation(@"senko-kick is not setuid root: reinstall the package",
                        @"у senko-kick нет бита setuid root: переустановите пакет");
    SenkoAddTranslation(@"senkod is missing: reinstall the package",
                        @"senkod отсутствует: переустановите пакет");
    SenkoAddTranslation(@"another daemon start is still running",
                        @"другой запуск демона ещё выполняется");
    SenkoAddTranslation(@"senkod did not open its control socket",
                        @"senkod не открыл управляющий сокет");
    SenkoAddTranslation(@"This address only hands back a link to itself: the provider has not published a subscription feed behind it. Ask them for the real subscription link.",
                        @"По этому адресу отдаётся ссылка на него же: провайдер не опубликовал за ним подписку. Попросите у него настоящую ссылку на подписку.");
    SenkoAddTranslation(@"The Happ crypt5 bundle on this page could not be opened. It is either damaged or sealed with a key this build does not carry.",
                        @"Бандл Happ crypt5 на этой странице не открылся: он либо повреждён, либо запечатан ключом, которого нет в этой сборке.");
    SenkoAddTranslation(@"This address opens a web page instead of a subscription feed. Copy the subscription link the page offers, not the page address.",
                        @"По этому адресу открывается веб-страница, а не подписка. Скопируйте ссылку на подписку, которую предлагает страница, а не адрес самой страницы.");
    SenkoAddTranslation(@"Copy link", @"Копировать ссылку");
    SenkoAddTranslation(@"Copied", @"Скопировано");
    SenkoAddTranslation(@"Classic home screen", @"Классический экран");
    SenkoAddTranslation(@"The dome button instead of the status card",
                        @"Купольная кнопка вместо карточки состояния");
    SenkoAddTranslation(@"Senko failed to start %d times and is running with "
                         "the stock theme. The report is in Logs.",
                        @"Senko не смог запуститься %d раза и работает со "
                         "стандартной темой. Отчёт лежит в разделе «Логи».");
    SenkoAddTranslation(@"QR code", @"QR-код");
    SenkoAddTranslation(@"Import from file", @"Импорт из файла");
    SenkoAddTranslation(@"Delete all servers", @"Удалить все серверы");
    SenkoAddTranslation(@"Delete", @"Удалить");
    SenkoAddTranslation(@"Every server in the Manual group is removed. Subscriptions are not touched.", @"Все серверы из группы «Вручную» будут удалены. Подписки не затрагиваются.");
    SenkoAddTranslation(@"AmneziaWG: refresh", @"AmneziaWG: обновить");
    SenkoAddTranslation(@"AmneziaWG: check ping", @"AmneziaWG: проверить пинг");
    SenkoAddTranslation(@"AmneziaWG: edit details", @"AmneziaWG: изменить");
    SenkoAddTranslation(@"AmneziaWG: remove profile", @"AmneziaWG: удалить профиль");
    SenkoAddTranslation(@"No servers yet", @"Серверов пока нет");
    SenkoAddTranslation(@"To use a server add a proxy link or a subscription.", @"Чтобы пользоваться сервером, добавьте прокси или подписку.");
    SenkoAddTranslation(@"Device ID (tap to copy)", @"ID устройства (нажмите, чтобы скопировать)");
    SenkoAddTranslation(@"Device ID copied", @"ID устройства скопирован");
    SenkoAddTranslation(@"not available yet", @"пока недоступен");
    SenkoAddTranslation(@"Unknown content type. This is not a server link, a subscription, or a profile Senko can read.", @"Неизвестный тип контента. Это не ссылка на сервер, не подписка и не профиль, который Senko умеет читать.");
    SenkoAddTranslation(@"No server Senko can run was found in this content.", @"В этом содержимом нет ни одного сервера, который Senko может запустить.");
    SenkoAddTranslation(@"Every server in this content is already saved.", @"Все серверы из этого содержимого уже сохранены.");
    SenkoAddTranslation(@"There was nothing to import.", @"Импортировать нечего.");
    SenkoAddTranslation(@"No configuration is selected. Pick a server first.", @"Ни одна конфигурация не выбрана. Сначала выберите сервер.");
    SenkoAddTranslation(@"The clipboard is empty.", @"Буфер обмена пуст.");
    SenkoAddTranslation(@"The file is empty or could not be read.", @"Файл пуст или его не удалось прочитать.");
    SenkoAddTranslation(@"The subscription refused this device.", @"Подписка отклонила это устройство.");
    SenkoAddTranslation(@"OK", @"ОК");
    SenkoAddTranslation(@"OFF", @"ВЫКЛ");
    SenkoAddTranslation(@"ON", @"ВКЛ");
    SenkoAddTranslation(@"Ping All", @"Проверить");
    SenkoAddTranslation(@"None", @"Нет");
    SenkoAddTranslation(@"none", @"нет");
    SenkoAddTranslation(@"ADDRESS", @"АДРЕС");
    SenkoAddTranslation(@"FLOW", @"FLOW");
    SenkoAddTranslation(@"PATH", @"ПУТЬ");
    SenkoAddTranslation(@"SNI", @"SNI");
    SenkoAddTranslation(@"FINGERPRINT", @"ОТПЕЧАТОК");
    SenkoAddTranslation(@"NAME", @"ИМЯ");
    SenkoAddTranslation(@"PORT", @"ПОРТ");
    SenkoAddTranslation(@"Preparing package", @"Подготовка пакета");
    SenkoAddTranslation(@"Refresh", @"Обновить");
    SenkoAddTranslation(@"Refresh now", @"Обновить сейчас");
    SenkoAddTranslation(@"Refresh the subscription to change it", @"Обновите подписку, чтобы изменить её");
    SenkoAddTranslation(@"Remove", @"Удалить");
    SenkoAddTranslation(@"Restarting senkod", @"Перезапуск senkod");
    SenkoAddTranslation(@"The full-device tunnel could not start. Open System Logs to see whether utun, routes, or the bundled core failed.", @"Не удалось запустить туннель всего устройства. Откройте системные логи: там указано, что именно не сработало, utun, маршруты или встроенное ядро.");
    SenkoAddTranslation(@"Running dpkg --install", @"Выполнение dpkg --install");
    SenkoAddTranslation(@"Russian", @"Русский");
    SenkoAddTranslation(@"Russian/English", @"Русский/Английский");
    SenkoAddTranslation(@"Save", @"Сохранить");
    SenkoAddTranslation(@"Senko", @"Сенко :3");
    SenkoAddTranslation(@"Scan QR", @"Сканировать QR");
    SenkoAddTranslation(@"Scan native config", @"Сканировать конфигурацию");
    SenkoAddTranslation(@"Settings", @"Настройки");
    SenkoAddTranslation(@"Starting services", @"Запуск служб");
    SenkoAddTranslation(@"Starting", @"Запуск");
    SenkoAddTranslation(@"State", @"Состояние");
    SenkoAddTranslation(@"Stopping senkod", @"Остановка senkod");
    SenkoAddTranslation(@"Stopping services", @"Остановка служб");
    SenkoAddTranslation(@"Style", @"Стиль");
    SenkoAddTranslation(@"Subscription", @"Подписка");
    SenkoAddTranslation(@"Subscription profile", @"Профиль подписки");
    SenkoAddTranslation(@"Subscription URL", @"URL подписки");
    SenkoAddTranslation(@"Header: value", @"Заголовок: значение");
    SenkoAddTranslation(@"HWID requires a Cookie request header", @"HWID требует заголовок Cookie");
    SenkoAddTranslation(@"Request header", @"Заголовок запроса");
    SenkoAddTranslation(@"System Logs", @"Системные логи");
    SenkoAddTranslation(@"senkod + awg combined", @"senkod + awg вместе");
    SenkoAddTranslation(@"Themes", @"Темы");
    SenkoAddTranslation(@"Update", @"Обновить");
    SenkoAddTranslation(@"Update Senko", @"Обновить Senko");
    SenkoAddTranslation(@"UUID", @"UUID");
    SenkoAddTranslation(@"UTILITIES", @"УТИЛИТЫ");
    SenkoAddTranslation(@"Version", @"Версия");
    SenkoAddTranslation(@"choose a .deb package", @"выберите пакет .deb");
    SenkoAddTranslation(@"daemon offline", @"демон недоступен");
    SenkoAddTranslation(@"daemon still offline", @"демон всё ещё недоступен");
    SenkoAddTranslation(@"daemon started", @"демон запущен");
    SenkoAddTranslation(@"daemon start failed", @"не удалось запустить демон");
    SenkoAddTranslation(@"senko-kick is not setuid root: reinstall the package",
                        @"у senko-kick нет прав root: переустановите пакет");
    SenkoAddTranslation(@"senkod is missing: reinstall the package",
                        @"senkod отсутствует: переустановите пакет");
    SenkoAddTranslation(@"another daemon start is still running",
                        @"запуск демона уже выполняется");
    SenkoAddTranslation(@"senkod did not open its control socket",
                        @"senkod не открыл управляющий сокет");
    SenkoAddTranslation(@"disconnect to edit", @"отключитесь для редактирования");
    SenkoAddTranslation(@"disconnect to remove", @"отключитесь для удаления");
    SenkoAddTranslation(@"disconnect to reorder", @"отключитесь для изменения порядка");
    SenkoAddTranslation(@"disconnect to switch", @"отключитесь для переключения");
    SenkoAddTranslation(@"disconnect to switch backend", @"отключитесь для смены режима");
    SenkoAddTranslation(@"fetch failed: daemon offline", @"не удалось получить данные: демон недоступен");
    SenkoAddTranslation(@"fetching subscription...", @"получение подписки...");
    SenkoAddTranslation(@"file import failed", @"не удалось импортировать файл");
    SenkoAddTranslation(@"folder", @"папка");
    SenkoAddTranslation(@"full-device", @"всё устройство");
    SenkoAddTranslation(@"group ping complete", @"проверка пинга группы завершена");
    SenkoAddTranslation(@"group profile check complete", @"проверка профилей группы завершена");
    SenkoAddTranslation(@"install a .deb", @"установить .deb");
    SenkoAddTranslation(@"install this package over the current version? settings and subscriptions stay in place", @"установить этот пакет поверх текущей версии? настройки и подписки сохранятся");
    SenkoAddTranslation(@"invalid amneziawg config", @"некорректная конфигурация amneziawg");
    SenkoAddTranslation(@"invalid native AmneziaWG config", @"некорректная нативная конфигурация AmneziaWG");
    SenkoAddTranslation(@"manual profiles only", @"только ручные профили");
    SenkoAddTranslation(@"manual", @"вручную");
    SenkoAddTranslation(@"name and url required", @"нужны название и URL");
    SenkoAddTranslation(@"native AmneziaWG config added", @"нативная конфигурация AmneziaWG добавлена");
    SenkoAddTranslation(@"no readable folders", @"нет доступных папок");
    SenkoAddTranslation(@"no servers in group", @"в группе нет серверов");
    SenkoAddTranslation(@"no servers to ping", @"нет серверов для проверки");
    SenkoAddTranslation(@"paste a link here", @"вставьте ссылку");
    SenkoAddTranslation(@"paste a subscription URL", @"вставьте URL подписки");
    SenkoAddTranslation(@"pick a server first", @"сначала выберите сервер");
    SenkoAddTranslation(@"ping check complete", @"проверка пинга завершена");
    SenkoAddTranslation(@"profile check complete", @"проверка профилей завершена");
    SenkoAddTranslation(@"QR code is empty or unreadable", @"QR-код пуст или не читается");
    SenkoAddTranslation(@"refreshing subscription...", @"обновление подписки...");
    SenkoAddTranslation(@"refreshing subscriptions...", @"обновление подписок...");
    SenkoAddTranslation(@"saving subscription...", @"сохранение подписки...");
    SenkoAddTranslation(@"section moved", @"раздел перемещён");
    SenkoAddTranslation(@"server moved", @"сервер перемещён");
    SenkoAddTranslation(@"list reloaded", @"список обновлён");
    SenkoAddTranslation(@"starting amneziawg...", @"запуск amneziawg...");
    SenkoAddTranslation(@"subscription added", @"подписка добавлена");
    SenkoAddTranslation(@"subscription not found", @"подписка не найдена");
    SenkoAddTranslation(@"subscription pinned", @"подписка закреплена");
    SenkoAddTranslation(@"subscription removed", @"подписка удалена");
    SenkoAddTranslation(@"removing subscription...", @"удаление подписки...");
    SenkoAddTranslation(@"subscription saved", @"подписка сохранена");
    SenkoAddTranslation(@"daemon offline: cannot save header", @"демон недоступен: нельзя сохранить заголовок");
    SenkoAddTranslation(@"subscription updated", @"подписка обновлена");
    SenkoAddTranslation(@"subscription url has spaces", @"в URL подписки есть пробелы");
    SenkoAddTranslation(@"subscriptions refreshed", @"подписки обновлены");
    SenkoAddTranslation(@"switch timeout", @"тайм-аут переключения");
    SenkoAddTranslation(@"timeout", @"тайм-аут");
    SenkoAddTranslation(@"validating native config...", @"проверка нативной конфигурации...");
    SenkoAddTranslation(@"amneziawg config not found", @"конфигурация amneziawg не найдена");
    SenkoAddTranslation(@"amneziawg profile loaded", @"профиль amneziawg загружен");
    SenkoAddTranslation(@"amneziawg profile removed", @"профиль amneziawg удалён");
    SenkoAddTranslation(@"amneziawg profile saved", @"профиль amneziawg сохранён");
    SenkoAddTranslation(@"amneziawg timeout", @"тайм-аут amneziawg");
    SenkoAddTranslation(@"could not read amneziawg config", @"не удалось прочитать конфигурацию amneziawg");
    SenkoAddTranslation(@"could not save amneziawg config", @"не удалось сохранить конфигурацию amneziawg");
    SenkoAddTranslation(@"could not save native AmneziaWG config", @"не удалось сохранить нативную конфигурацию AmneziaWG");
    SenkoAddTranslation(@"could not start amneziawg", @"не удалось запустить amneziawg");
    SenkoAddTranslation(@"could not stop amneziawg", @"не удалось остановить amneziawg");
    SenkoAddTranslation(@"could not stop senkod", @"не удалось остановить senkod");
    SenkoAddTranslation(@"daemon offline: cannot add", @"демон недоступен: нельзя добавить");
    SenkoAddTranslation(@"daemon offline: cannot edit", @"демон недоступен: нельзя изменить");
    SenkoAddTranslation(@"daemon offline: cannot import", @"демон недоступен: нельзя импортировать");
    SenkoAddTranslation(@"checking amneziawg...", @"проверка amneziawg...");
    SenkoAddTranslation(@"checking daemon...", @"проверка демона...");
    SenkoAddTranslation(@"checking group ping...", @"проверка пинга группы...");
    SenkoAddTranslation(@"checking ping...", @"проверка пинга...");
    SenkoAddTranslation(@"checking server...", @"проверка сервера...");
    SenkoAddTranslation(@"server ping timeout", @"сервер не ответил вовремя");
    SenkoAddTranslation(@"server ping", @"пинг сервера");
    SenkoAddTranslation(@"failed", @"ошибка");
    SenkoAddTranslation(@"checking", @"проверка");
    SenkoAddTranslation(@"WAIT", @"ЖДИТЕ");
    SenkoAddTranslation(@"cannot open folder", @"не удалось открыть папку");
    SenkoAddTranslation(@"empty folder", @"папка пуста");
    SenkoAddTranslation(@"Senko does not support this protocol", @"Senko не поддерживает этот протокол");
    SenkoAddTranslation(@"awg / udp / full-device", @"awg / udp / всё устройство");
    SenkoAddTranslation(@"Send HWID in Cookie", @"Отправлять HWID в cookie");
    SenkoAddTranslation(@"Title and URL", @"Название и URL");
    SenkoAddTranslation(@"This theme will lag on iOS 6/7. Liquid glass is laggy on older device.", @"Эта тема будет тормозить на iOS 6/7. Liquid glass медленный на старых устройствах");
    SenkoAddTranslation(@"Dark / Light applies to the selected style. Choice is stored on device.", @"Тёмная или светлая тема зависит от выбранного стиля. Выбор сохраняется на устройстве");
    SenkoAddTranslation(@"Play a short ouch on every button tap.", @"Проигрывать короткий ouch при каждом нажатии");
    SenkoAddTranslation(@"Play a short meow on every button tap.", @"Проигрывать короткий meow при каждом нажатии");
    SenkoAddTranslation(@"Senko-Miside is Dark only: pattern wallpaper and candy heart ON.", @"Senko-Miside только тёмная: узор на обоях и конфетное сердце включены");
    SenkoAddTranslation(@"Senko-Boykisser: pink paper or rose ink, with falling boykissers on the home screen.",
                        @"Senko-Boykisser: розовая бумага или тёмно-розовые чернила, с падающими boykisser на главном экране.");
    SenkoAddTranslation(@"Senko-Aero is Light only: sky wallpaper and floating gloss bubbles.", @"Senko-Aero только светлая: обои с небом и парящие глянцевые пузыри");
    SenkoAddTranslation(@"the legacy theme for the legacy community", @"для выживших на iOS 6 :D");
    SenkoAddTranslation(@"flat and transparent", @"эстетика 2013 года");
    SenkoAddTranslation(@"meeeeeow :3", @"самая фембойская тема");
    SenkoAddTranslation(@"hehehe mita hehehe miside", @"ыыыыы кепочка ыыыыы мисайд");
    SenkoAddTranslation(@"futuristic maximalism of the past", @"эстетика, опередившая свое время");
    SenkoAddTranslation(@"modern theme", @"dopamine, palera1n, trollstore и вайб 2021 года");
    SenkoAddTranslation(@"liquid ass... nah, glass", @"нууу такое... tahoe!");
    SenkoAddTranslation(@"QR code not detected\nfill the frame with the code\nand hold the phone still", @"QR-код не обнаружен\nзаполните кадр кодом\nи держите телефон неподвижно");
    SenkoAddTranslation(@"point the camera at a QR code\nserver link, subscription URL\nor a WireGuard / AmneziaWG .conf", @"наведите камеру на QR-код\nссылка на сервер, URL подписки\nили .conf WireGuard / AmneziaWG");
    SenkoAddTranslation(@"Amnezia VPN bundle detected. Export a native AmneziaWG .conf from Share", @"Обнаружен пакет Amnezia VPN. Экспортируйте нативный файл AmneziaWG .conf через Share");
    SenkoAddTranslation(@"Amnezia VPN bundle detected. Import a native AmneziaWG .conf file", @"Обнаружен пакет Amnezia VPN. Импортируйте нативный файл AmneziaWG .conf");
    SenkoAddTranslation(@"A live profile cannot be edited", @"Активный профиль нельзя изменить");
    SenkoAddTranslation(@"SOCKS is localhost-only by default. socks_public=1 in config opens it to the LAN.", @"SOCKS по умолчанию доступен только локально. socks_public=1 в конфигурации открывает его для сети");
    SenkoAddTranslation(@"Starting install helper", @"Запуск установщика");
    SenkoAddTranslation(@"Tap Close when you are ready.", @"Нажмите «Закрыть», когда будете готовы");
    SenkoAddTranslation(@"unknown error", @"неизвестная ошибка");
    SenkoAddTranslation(@"Could not connect to the server. Check the address, network, and server availability.", @"Не удалось подключиться к серверу. Проверьте адрес, сеть и доступность сервера.");
    SenkoAddTranslation(@"The tunnel could not be opened. Check the server settings, key, and selected transport.", @"Не удалось открыть туннель. Проверьте параметры сервера, ключ и выбранный транспорт.");
    SenkoAddTranslation(@"The local proxy could not start. Restart Senko and check that another copy is not running.", @"Не удалось запустить локальный прокси. Перезапустите Senko и проверьте, что другая копия не запущена.");
    SenkoAddTranslation(@"The server name could not be resolved. Check the internet connection and server address.", @"Не удалось найти сервер по имени. Проверьте интернет-соединение и адрес сервера.");
    SenkoAddTranslation(@"The system firewall could not apply Senko routing rules. Open System Logs and check the last pfctl message.", @"Системный firewall не смог применить правила Senko. Откройте «Системные логи» и посмотрите последнее сообщение pfctl.");
    SenkoAddTranslation(@"The firewall rules were accepted, but device traffic was not redirected. This jailbreak does not expose a working full-device routing path.", @"Правила firewall приняты, но трафик устройства не перенаправляется. Этот jailbreak не предоставляет рабочий маршрут для всего устройства.");
    SenkoAddTranslation(@"This server uses a protocol or security mode that Senko does not support.", @"Этот сервер использует протокол или режим защиты, который Senko не поддерживает.");
    SenkoAddTranslation(@"The server link has an invalid UUID. Import the link again from its source.", @"В ссылке сервера неверный UUID. Импортируйте ссылку заново из источника.");
    SenkoAddTranslation(@"The connection attempt timed out. Check the network and try another server.", @"Время ожидания подключения истекло. Проверьте сеть и попробуйте другой сервер.");
    SenkoAddTranslation(@"The Senko service is not responding. Restart it and try again.", @"Служба Senko не отвечает. Перезапустите её и повторите попытку.");
    SenkoAddTranslation(@"The server port is reachable, but the profile could not complete a real connection. Check its UUID or password, security, SNI, and transport settings.", @"Порт сервера доступен, но профиль не смог установить настоящее соединение. Проверьте UUID или пароль, защиту, SNI и транспорт.");
    SenkoAddTranslation(@"This profile cannot be checked while another profile is connected. Disconnect first.", @"Нельзя проверить этот профиль, пока подключён другой. Сначала отключитесь.");
    SenkoAddTranslation(@"The server address is invalid, unsafe, or cannot be resolved.", @"Адрес сервера некорректен, небезопасен или не определяется через DNS.");
    SenkoAddTranslation(@"See /tmp/senko-update.log", @"См. /tmp/senko-update.log");
    SenkoAddTranslation(@"(no log)", @"(нет лога)");

/* theme editor */
    SenkoAddTranslation(@"STYLE", @"СТИЛЬ");
    SenkoAddTranslation(@"VARIANT", @"ВАРИАНТ");
    SenkoAddTranslation(@"LIGHT COLORS", @"СВЕТЛЫЕ ЦВЕТА");
    SenkoAddTranslation(@"DARK COLORS", @"ТЁМНЫЕ ЦВЕТА");
    SenkoAddTranslation(@"Theme name", @"Название темы");
    SenkoAddTranslation(@"Chrome", @"Панели");
    SenkoAddTranslation(@"Look", @"Вид");
    SenkoAddTranslation(@"Corners", @"Скругление");
    SenkoAddTranslation(@"Editing", @"Редактируем");
    SenkoAddTranslation(@"Classic", @"Классика");
    SenkoAddTranslation(@"Flat", @"Плоское");
    SenkoAddTranslation(@"Glass", @"Стекло");
    SenkoAddTranslation(@"Add dark variant", @"Добавить тёмный вариант");
    SenkoAddTranslation(@"Export to Documents", @"Экспорт в Documents");
    SenkoAddTranslation(@"Delete theme", @"Удалить тему");
    SenkoAddTranslation(@"Delete theme?", @"Удалить тему?");
    SenkoAddTranslation(@"New theme", @"Новая тема");
    SenkoAddTranslation(@"Copy current theme", @"Копировать текущую тему");
    SenkoAddTranslation(@"Import from Documents", @"Импорт из Documents");
    SenkoAddTranslation(@"Import theme", @"Импорт темы");
    SenkoAddTranslation(@"Theme exported", @"Тема экспортирована");
    SenkoAddTranslation(@"Theme imported", @"Тема импортирована");
    SenkoAddTranslation(@"Export failed", @"Не удалось экспортировать");
    SenkoAddTranslation(@"Import failed", @"Не удалось импортировать");
    SenkoAddTranslation(@"Could not create theme", @"Не удалось создать тему");
    SenkoAddTranslation(@"The custom theme limit is reached.", @"Достигнут предел числа своих тем.");
    SenkoAddTranslation(@"made on this device", @"сделана на этом устройстве");
    SenkoAddTranslation(@"Unknown error", @"Неизвестная ошибка");

/* server sorting */
    SenkoAddTranslation(@"Sort servers", @"Сортировка серверов");
    SenkoAddTranslation(@"Stored order", @"Как сохранено");
    SenkoAddTranslation(@"By name", @"По названию");
    SenkoAddTranslation(@"By latency", @"По задержке");

/* home screen: status card and the server detail sheet */
    SenkoAddTranslation(@"Connected", @"Подключено");
    SenkoAddTranslation(@"Connecting", @"Подключение");
    SenkoAddTranslation(@"Disconnected", @"Отключено");
    SenkoAddTranslation(@"Connect", @"Подключить");
    SenkoAddTranslation(@"Disconnect", @"Отключить");
    SenkoAddTranslation(@"No server selected", @"Сервер не выбран");
    SenkoAddTranslation(@"AmneziaWG profile", @"Профиль AmneziaWG");
    SenkoAddTranslation(@"Host", @"Адрес");
    SenkoAddTranslation(@"Protocol", @"Протокол");
    SenkoAddTranslation(@"Transport", @"Транспорт");
    SenkoAddTranslation(@"Security", @"Защита");
    SenkoAddTranslation(@"Latency", @"Задержка");
    SenkoAddTranslation(@"TCP latency", @"Задержка TCP");
    SenkoAddTranslation(@"Source", @"Источник");
    SenkoAddTranslation(@"Link", @"Ссылка");
    SenkoAddTranslation(@"loading", @"загрузка");
    SenkoAddTranslation(@"unavailable", @"недоступна");
    SenkoAddTranslation(@"copied", @"скопирована");
    SenkoAddTranslation(@"unreachable", @"нет ответа");
    SenkoAddTranslation(@"Ping", @"Пинг");
    SenkoAddTranslation(@"Timeout", @"Тайм-аут");
    SenkoAddTranslation(@"Checking connection", @"Проверка подключения");
    SenkoAddTranslation(@"Checking TCP", @"Проверка TCP");
    SenkoAddTranslation(@"Checking server", @"Проверка сервера");
    SenkoAddTranslation(@"Loading servers and subscriptions...", @"Загрузка серверов и подписок...");
    SenkoAddTranslation(@"Refreshing subscriptions", @"Обновление подписок");
    SenkoAddTranslation(@"Checking tunnel", @"Проверка туннеля");
    SenkoAddTranslation(@"Ping complete", @"Пинг завершён");
    SenkoAddTranslation(@"Copy", @"Копировать");
    SenkoAddTranslation(@"This build cannot dial this profile", @"Эта сборка не умеет подключаться к такому профилю");

/* theme editor: palette slots */
    SenkoAddTranslation(@"Background", @"Фон");
    SenkoAddTranslation(@"Background low", @"Фон снизу");
    SenkoAddTranslation(@"Felt", @"Подложка");
    SenkoAddTranslation(@"Online", @"Активно");
    SenkoAddTranslation(@"Online low", @"Активно снизу");
    SenkoAddTranslation(@"Offline", @"Неактивно");
    SenkoAddTranslation(@"Offline low", @"Неактивно снизу");
    SenkoAddTranslation(@"Text", @"Текст");
    SenkoAddTranslation(@"Text muted", @"Текст приглушённый");
    SenkoAddTranslation(@"Accent", @"Акцент");
    SenkoAddTranslation(@"Accent low", @"Акцент нажатый");
    SenkoAddTranslation(@"Chrome low", @"Панели снизу");
    SenkoAddTranslation(@"Cell", @"Ячейка");
    SenkoAddTranslation(@"Cell low", @"Ячейка снизу");
    SenkoAddTranslation(@"Well", @"Углубление");
    SenkoAddTranslation(@"wallpaper top", @"обои сверху");
    SenkoAddTranslation(@"wallpaper bottom", @"обои снизу");
    SenkoAddTranslation(@"list backdrop", @"фон списка");
    SenkoAddTranslation(@"connected button top", @"кнопка подключено, верх");
    SenkoAddTranslation(@"connected button bottom", @"кнопка подключено, низ");
    SenkoAddTranslation(@"disconnected button top", @"кнопка отключено, верх");
    SenkoAddTranslation(@"disconnected button bottom", @"кнопка отключено, низ");
    SenkoAddTranslation(@"primary label", @"основная надпись");
    SenkoAddTranslation(@"secondary label", @"второстепенная надпись");
    SenkoAddTranslation(@"links and glyphs", @"ссылки и значки");
    SenkoAddTranslation(@"pressed accent", @"акцент при нажатии");
    SenkoAddTranslation(@"bars top", @"панели сверху");
    SenkoAddTranslation(@"bars bottom", @"панели снизу");
    SenkoAddTranslation(@"row top", @"строка сверху");
    SenkoAddTranslation(@"row bottom", @"строка снизу");
    SenkoAddTranslation(@"list inset tint", @"подложка списка");

/* theme editor: failures surfaced from storage */
    SenkoAddTranslation(@"Theme is not a custom theme", @"Это не пользовательская тема");
    SenkoAddTranslation(@"Could not serialize theme", @"Не удалось сохранить тему в файл");
    SenkoAddTranslation(@"Could not write to Documents", @"Не удалось записать в Documents");
    SenkoAddTranslation(@"Not a Senko theme file", @"Это не файл темы Senko");
    SenkoAddTranslation(@"Could not read the file", @"Не удалось прочитать файл");
    SenkoAddTranslation(@"Unsupported theme format", @"Неподдерживаемый формат темы");
    SenkoAddTranslation(@"Theme file is incomplete", @"Файл темы неполный");
    SenkoAddTranslation(@"Custom theme limit reached", @"Достигнут предел числа своих тем");

/* common screen text: Chinese is kept here instead of relying on system
   strings, because the app runs on iOS 5 where no localization bundle exists */
    SenkoAddChineseTranslation(@"About", @"关于");
    SenkoAddChineseTranslation(@"Settings", @"设置");
    SenkoAddChineseTranslation(@"Language", @"语言");
    SenkoAddChineseTranslation(@"English", @"English");
    SenkoAddChineseTranslation(@"Russian", @"Русский");
    SenkoAddChineseTranslation(@"Chinese", @"中文");
    SenkoAddChineseTranslation(@"GENERAL", @"常规");
    SenkoAddChineseTranslation(@"AUTOMATION", @"自动化");
    SenkoAddChineseTranslation(@"ROUTING", @"路由");
    SenkoAddChineseTranslation(@"APP", @"应用");
    SenkoAddChineseTranslation(@"DEVELOPER", @"开发者");
    SenkoAddChineseTranslation(@"Server", @"服务器");
    SenkoAddChineseTranslation(@"SERVER", @"服务器");
    SenkoAddChineseTranslation(@"Add", @"添加");
    SenkoAddChineseTranslation(@"Add server", @"添加服务器");
    SenkoAddChineseTranslation(@"Add subscription", @"添加订阅");
    SenkoAddChineseTranslation(@"Cancel", @"取消");
    SenkoAddChineseTranslation(@"Close", @"关闭");
    SenkoAddChineseTranslation(@"OK", @"确定");
    SenkoAddChineseTranslation(@"Save", @"保存");
    SenkoAddChineseTranslation(@"Delete", @"删除");
    SenkoAddChineseTranslation(@"Remove", @"移除");
    SenkoAddChineseTranslation(@"Edit", @"编辑");
    SenkoAddChineseTranslation(@"Copy", @"复制");
    SenkoAddChineseTranslation(@"Copied", @"已复制");
    SenkoAddChineseTranslation(@"Copy link", @"复制链接");
    SenkoAddChineseTranslation(@"Done", @"完成");
    SenkoAddChineseTranslation(@"Complete", @"完成");
    SenkoAddChineseTranslation(@"Refresh", @"刷新");
    SenkoAddChineseTranslation(@"Refresh now", @"立即刷新");
    SenkoAddChineseTranslation(@"Update", @"更新");
    SenkoAddChineseTranslation(@"Update Senko", @"更新 Senko");
    SenkoAddChineseTranslation(@"Update subscriptions", @"更新订阅");
    SenkoAddChineseTranslation(@"Connect", @"连接");
    SenkoAddChineseTranslation(@"Disconnect", @"断开连接");
    SenkoAddChineseTranslation(@"Connected", @"已连接");
    SenkoAddChineseTranslation(@"Connecting", @"连接中");
    SenkoAddChineseTranslation(@"Disconnected", @"未连接");
    SenkoAddChineseTranslation(@"Connection failed", @"连接失败");
    SenkoAddChineseTranslation(@"Checking", @"检查中");
    SenkoAddChineseTranslation(@"Checking server", @"正在检查服务器");
    SenkoAddChineseTranslation(@"Loading servers and subscriptions...", @"正在加载服务器和订阅...");
    SenkoAddChineseTranslation(@"Checking connection", @"正在检查连接");
    SenkoAddChineseTranslation(@"Checking tunnel", @"正在检查隧道");
    SenkoAddChineseTranslation(@"Ping", @"Ping");
    SenkoAddChineseTranslation(@"Check ping", @"检查 Ping");
    SenkoAddChineseTranslation(@"Check servers", @"检查服务器");
    SenkoAddChineseTranslation(@"Ping complete", @"Ping 完成");
    SenkoAddChineseTranslation(@"Timeout", @"超时");
    SenkoAddChineseTranslation(@"no servers to ping", @"没有可 Ping 的服务器");
    SenkoAddChineseTranslation(@"State", @"状态");
    SenkoAddChineseTranslation(@"Version", @"版本");
    SenkoAddChineseTranslation(@"Sort servers", @"服务器排序");
    SenkoAddChineseTranslation(@"Stored order", @"保存的顺序");
    SenkoAddChineseTranslation(@"By name", @"按名称");
    SenkoAddChineseTranslation(@"By latency", @"按延迟");
    SenkoAddChineseTranslation(@"No server selected", @"未选择服务器");
    SenkoAddChineseTranslation(@"No servers yet", @"还没有服务器");
    SenkoAddChineseTranslation(@"To use a server add a proxy link or a subscription.", @"请添加代理链接或订阅以使用服务器。");
    SenkoAddChineseTranslation(@"Connect at startup", @"启动时连接");
    SenkoAddChineseTranslation(@"Dial the selected server after a reboot", @"重启后连接选中的服务器");
    SenkoAddChineseTranslation(@"Reconnect automatically", @"自动重连");
    SenkoAddChineseTranslation(@"After a drop or a change of network", @"断线或网络变化后");
    SenkoAddChineseTranslation(@"Try another server", @"尝试其他服务器");
    SenkoAddChineseTranslation(@"Only inside the same section, fastest first", @"仅在同一分组内，优先最快的服务器");
    SenkoAddChineseTranslation(@"Off", @"关闭");
    SenkoAddChineseTranslation(@"Every %d h", @"每 %d 小时");
    SenkoAddChineseTranslation(@"Reconnect attempts", @"重连次数");
    SenkoAddChineseTranslation(@"Until it works", @"直到成功");
    SenkoAddChineseTranslation(@"%d attempts", @"%d 次");
    SenkoAddChineseTranslation(@"Routing rules", @"路由规则");
    SenkoAddChineseTranslation(@"Send a domain or a subnet direct, or block it", @"让域名或网段直连，或阻止它");
    SenkoAddChineseTranslation(@"Direct", @"直连");
    SenkoAddChineseTranslation(@"Block", @"阻止");
    SenkoAddChineseTranslation(@"Through the tunnel", @"通过隧道");
    SenkoAddChineseTranslation(@"Domain and subdomains", @"域名及子域名");
    SenkoAddChineseTranslation(@"Keyword", @"关键词");
    SenkoAddChineseTranslation(@"IP range", @"IP 网段");
    SenkoAddChineseTranslation(@"What should happen to the traffic?", @"如何处理流量？");
    SenkoAddChineseTranslation(@"What should it match?", @"匹配什么？");
    SenkoAddChineseTranslation(@"Value", @"值");
    SenkoAddChineseTranslation(@"Theme", @"主题");
    SenkoAddChineseTranslation(@"Themes", @"主题");
    SenkoAddChineseTranslation(@"Appearance", @"外观");
    SenkoAddChineseTranslation(@"Dark", @"深色");
    SenkoAddChineseTranslation(@"Light", @"浅色");
    SenkoAddChineseTranslation(@"Classic home screen", @"经典主屏幕");
    SenkoAddChineseTranslation(@"The dome button instead of the status card", @"使用圆顶按钮代替状态卡片");
    SenkoAddChineseTranslation(@"System Logs", @"系统日志");
    SenkoAddChineseTranslation(@"senkod + awg combined", @"senkod + awg 合并日志");
    SenkoAddChineseTranslation(@"Export backup", @"导出备份");
    SenkoAddChineseTranslation(@"Restore backup", @"恢复备份");
    SenkoAddChineseTranslation(@"Save a config file to Documents", @"将配置文件保存到 Documents");
    SenkoAddChineseTranslation(@"Validate, then replace configuration", @"验证后替换配置");
    SenkoAddChineseTranslation(@"Choose a Senko .deb package", @"选择 Senko .deb 软件包");
    SenkoAddChineseTranslation(@"Developer", @"开发者");
    SenkoAddChineseTranslation(@"What was chosen, checks, overrides and rescue", @"选择项、检查、覆盖和恢复工具");
    SenkoAddChineseTranslation(@"Transport", @"传输");
    SenkoAddChineseTranslation(@"Security", @"安全");
    SenkoAddChineseTranslation(@"Latency", @"延迟");
    SenkoAddChineseTranslation(@"TCP latency", @"TCP 延迟");
    SenkoAddChineseTranslation(@"Host", @"地址");
    SenkoAddChineseTranslation(@"Protocol", @"协议");
    SenkoAddChineseTranslation(@"PASSWORD", @"密码");
    SenkoAddChineseTranslation(@"UUID", @"UUID");
    SenkoAddChineseTranslation(@"ADDRESS", @"地址");
    SenkoAddChineseTranslation(@"PORT", @"端口");
    SenkoAddChineseTranslation(@"SNI", @"SNI");
    SenkoAddChineseTranslation(@"NAME", @"名称");
    SenkoAddChineseTranslation(@"Subscription", @"订阅");
    SenkoAddChineseTranslation(@"Subscription details", @"订阅详情");
    SenkoAddChineseTranslation(@"Subscription URL", @"订阅 URL");
    SenkoAddChineseTranslation(@"Title and URL", @"标题和 URL");
    SenkoAddChineseTranslation(@"Manual", @"手动");
    SenkoAddChineseTranslation(@"Not provided", @"未提供");
    SenkoAddChineseTranslation(@"Used", @"已使用");
    SenkoAddChineseTranslation(@"Remaining", @"剩余");
    SenkoAddChineseTranslation(@"Limit", @"上限");
    SenkoAddChineseTranslation(@"Uploaded", @"上传");
    SenkoAddChineseTranslation(@"Downloaded", @"下载");
    SenkoAddChineseTranslation(@"Expires", @"到期");
    SenkoAddChineseTranslation(@"Expired", @"已过期");
    SenkoAddChineseTranslation(@"Description", @"描述");
    SenkoAddChineseTranslation(@"Contact support", @"联系支持");
    SenkoAddChineseTranslation(@"Paste", @"粘贴");
    SenkoAddChineseTranslation(@"Paste from clipboard", @"从剪贴板粘贴");
    SenkoAddChineseTranslation(@"QR code", @"二维码");
    SenkoAddChineseTranslation(@"Import from file", @"从文件导入");
    SenkoAddChineseTranslation(@"Import file", @"导入文件");
    SenkoAddChineseTranslation(@"Scan QR", @"扫描二维码");
    SenkoAddChineseTranslation(@"Delete all servers", @"删除所有服务器");
    SenkoAddChineseTranslation(@"Every server in the Manual group is removed. Subscriptions are not touched.", @"将删除手动分组中的所有服务器，不会修改订阅。");
    SenkoAddChineseTranslation(@"Configuration backup", @"配置备份");
    SenkoAddChineseTranslation(@"Replace configuration?", @"替换配置？");
    SenkoAddChineseTranslation(@"The imported backup will replace all current servers and subscriptions.", @"导入的备份将替换当前所有服务器和订阅。");
    SenkoAddChineseTranslation(@"Configuration restored", @"配置已恢复");
    SenkoAddChineseTranslation(@"No daemon logs available", @"没有可用的 daemon 日志");
    SenkoAddChineseTranslation(@"Loading logs...", @"正在加载日志...");
    SenkoAddChineseTranslation(@"Error", @"错误");
    SenkoAddChineseTranslation(@"Failed", @"失败");
    SenkoAddChineseTranslation(@"Install failed", @"安装失败");
    SenkoAddChineseTranslation(@"Installing", @"正在安装");
    SenkoAddChineseTranslation(@"Starting", @"正在启动");
    SenkoAddChineseTranslation(@"Starting services", @"正在启动服务");
    SenkoAddChineseTranslation(@"Stopping services", @"正在停止服务");
    SenkoAddChineseTranslation(@"Restarting senkod", @"正在重启 senkod");
    SenkoAddChineseTranslation(@"daemon offline", @"daemon 离线");
    SenkoAddChineseTranslation(@"daemon started", @"daemon 已启动");
    SenkoAddChineseTranslation(@"Senko", @"Senko");
    SenkoAddChineseTranslation(@"No server in the catalog", @"目录中没有服务器");
    SenkoAddChineseTranslation(@"No servers in the catalog.", @"目录中没有服务器。");
    SenkoAddChineseTranslation(@"Unknown error", @"未知错误");
    SenkoAddChineseTranslation(@"The report is on the clipboard.", @"报告已复制到剪贴板。");
    SenkoAddChineseTranslation(@"Copied to the clipboard.", @"已复制到剪贴板。");
    SenkoAddChineseTranslation(@"Special thanks: @CookieValerka, @inraxx, @not_a_modder, @s3dativee, @Lineysom, @shizotoaster", @"特别感谢：@CookieValerka、@inraxx、@not_a_modder、@s3dativee、@Lineysom、@shizotoaster");
}

BOOL SenkoLanguageIsRussian(void) {
    return [[NSUserDefaults standardUserDefaults] integerForKey:SENKO_LANGUAGE_KEY] == SenkoLanguageRussian;
}

BOOL SenkoLanguageIsChinese(void) {
    return [[NSUserDefaults standardUserDefaults] integerForKey:SENKO_LANGUAGE_KEY] == SenkoLanguageChinese;
}

NSString *SenkoRedactSecrets(NSString *text) {
    if (![text length]) return text;
    NSMutableString *safe = [NSMutableString stringWithString:text];
    NSArray *rules = [NSArray arrayWithObjects:
        @"(?i)(authorization|proxy-authorization|cookie|set-cookie|privatekey|presharedkey)\\s*[:=]\\s*[^\\r\\n]+",
        @"(://)[^/@\\s]+@",
        @"(?i)([?&](token|key|password|pass|uuid|pbk|sid)=)[^&#\\s]+", nil];
    NSArray *replacements = [NSArray arrayWithObjects:@"$1: <redacted>",
        @"$1<redacted>@", @"$1<redacted>", nil];
    for (NSUInteger i = 0; i < [rules count]; ++i) {
        NSRegularExpression *rx = [NSRegularExpression
            regularExpressionWithPattern:[rules objectAtIndex:i] options:0 error:NULL];
        if (!rx) continue;
        [rx replaceMatchesInString:safe options:0 range:NSMakeRange(0, [safe length])
                       withTemplate:[replacements objectAtIndex:i]];
    }
    return safe;
}

/* the daemon prefixes the panel's own wording, which stays untranslated */
static NSString * const kGatePrefix = @"subscription refused this device: ";

NSString *SenkoHumanReadableError(NSString *text) {
    NSString *raw = [text stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([raw hasPrefix:@"ERR "]) raw = [raw substringFromIndex:4];
    NSString *message = nil;
    if ([raw isEqualToString:@"socks: tunnel open failed"] ||
        [raw isEqualToString:@"socks: tunnel verify failed"])
        message = @"The tunnel could not be opened. Check the server settings, key, and selected transport.";
    else if ([raw isEqualToString:@"socks: socks listener failed"] ||
             [raw isEqualToString:@"socks: socks listen connect failed"])
        message = @"The local proxy could not start. Restart Senko and check that another copy is not running.";
    else if ([raw isEqualToString:@"server: dns resolution failed"])
        message = @"The server name could not be resolved. Check the internet connection and server address.";
    else if ([raw isEqualToString:@"routing: routing setup failed"])
        message = @"The system firewall could not apply Senko routing rules. Open System Logs and check the last pfctl message.";
    else if ([raw isEqualToString:@"routing: routing rules accepted but traffic was not redirected"])
        message = @"The firewall rules were accepted, but device traffic was not redirected. This jailbreak does not expose a working full-device routing path.";
    else if ([raw isEqualToString:@"tunnel: the go backend core could not start; open System Logs for the exact cause"])
        message = @"The full-device tunnel could not start. Open System Logs to see whether utun, routes, or the bundled core failed.";
    else if ([raw isEqualToString:@"server: unsupported protocol or security"])
        message = @"This server uses a protocol or security mode that Senko does not support.";
    else if ([raw isEqualToString:@"server: bad uuid in server link"])
        message = @"The server link has an invalid UUID. Import the link again from its source.";
    else if ([raw hasPrefix:@"this address only hands back its own link"])
        message = @"This address only hands back a link to itself: the provider has not published a subscription feed behind it. Ask them for the real subscription link.";
    else if ([raw hasPrefix:@"the happ crypt5 bundle on this page could not be opened"])
        message = @"The Happ crypt5 bundle on this page could not be opened. It is either damaged or sealed with a key this build does not carry.";
    else if ([raw hasPrefix:@"this address opens a web page"])
        message = @"This address opens a web page instead of a subscription feed. Copy the subscription link the page offers, not the page address.";
    else if ([raw isEqualToString:@"connect timeout"] || [raw isEqualToString:@"switch timeout"])
        message = @"The connection attempt timed out. Check the network and try another server.";
    else if ([raw hasPrefix:@"daemon offline"] || [raw isEqualToString:@"daemon offline"])
        message = @"The Senko service is not responding. Restart it and try again.";
    else if ([raw isEqualToString:@"server: no working server"])
        message = @"Could not connect to the server. Check the address, network, and server availability.";
    else if ([raw hasPrefix:@"profile handshake failed:"])
        message = @"The server port is reachable, but the profile could not complete a real connection. Check its UUID or password, security, SNI, and transport settings.";
    else if ([raw isEqualToString:@"disconnect before checking another profile"])
        message = @"This profile cannot be checked while another profile is connected. Disconnect first.";
    else if ([raw isEqualToString:@"server address is invalid"] ||
             [raw isEqualToString:@"server address is unsafe or cannot be resolved"] ||
             [raw isEqualToString:@"unsafe or unresolved address"])
        message = @"The server address is invalid, unsafe, or cannot be resolved.";
    else if ([raw isEqualToString:@"profile uses an unsupported transport or security mode"])
        message = @"This server uses a protocol or security mode that Senko does not support.";
    else if ([raw isEqualToString:@"profile has an invalid UUID"])
        message = @"The server link has an invalid UUID. Import the link again from its source.";
    else if ([raw hasPrefix:@"socks:"])
        message = @"The tunnel could not be opened. Check the server settings, key, and selected transport.";
    else if ([raw hasPrefix:@"routing:"] || [raw hasPrefix:@"error route"])
        message = @"The system firewall could not apply Senko routing rules. Open System Logs and check the last pfctl message.";
    else if ([raw hasPrefix:@"error endpoint udp"])
        message = @"Could not connect to the server. Check the address, network, and server availability.";
    else if ([raw hasPrefix:@"error utun"] || [raw hasPrefix:@"error route"])
        message = @"The system firewall could not apply Senko routing rules. Open System Logs and check the last pfctl message.";
    else if ([raw isEqualToString:@"unknown content type"])
        message = @"Unknown content type. This is not a server link, a subscription, or a profile Senko can read.";
    else if ([raw isEqualToString:@"no server senko can run in this file"])
        message = @"No server Senko can run was found in this content.";
    else if ([raw isEqualToString:@"every server in this file is already saved"])
        message = @"Every server in this content is already saved.";
    else if ([raw isEqualToString:@"nothing to import"])
        message = @"There was nothing to import.";
    else if ([raw isEqualToString:@"no configuration is selected"])
        message = @"No configuration is selected. Pick a server first.";
    else if ([raw isEqualToString:@"the clipboard is empty"])
        message = @"The clipboard is empty.";
    else if ([raw isEqualToString:@"the file is empty or could not be read"])
        message = @"The file is empty or could not be read.";
    else if ([raw hasPrefix:kGatePrefix])
        return [SenkoLocalizedText(@"The subscription refused this device.")
                stringByAppendingFormat:@" %@",
                [raw substringFromIndex:[kGatePrefix length]]];
    return SenkoLocalizedText(message ? message : raw);
}

NSString *SenkoLanguageName(void) {
    if (SenkoLanguageIsChinese()) return @"中文";
    return SenkoLanguageIsRussian() ? @"Русский" : @"English";
}

static NSString *SenkoRussianPlural(NSInteger number,
                                    NSString *one,
                                    NSString *few,
                                    NSString *many) {
    NSInteger n = number < 0 ? -number : number;
    NSInteger last = n % 10;
    NSInteger lastTwo = n % 100;
    if (last == 1 && lastTwo != 11) return one;
    if (last >= 2 && last <= 4 && (lastTwo < 12 || lastTwo > 14)) return few;
    return many;
}

static BOOL SenkoAllDigits(NSString *text) {
    if (![text length]) return NO;
    for (NSUInteger i = 0; i < [text length]; ++i) {
        unichar c = [text characterAtIndex:i];
        if (c < '0' || c > '9') return NO;
    }
    return YES;
}

/* bulk actions answer with "<verb> <n> server(s)[ tail]", and the bare count
   rule below would read the verb as the number */
static NSString *SenkoServerCountReply(NSString *text) {
    static NSString * const kVerbs[] = { @"imported ", @"removed ", @"refreshed " };
    static NSString * const kRussian[] = { @"импортировано", @"удалено", @"обновлено" };
    for (size_t i = 0; i < sizeof kVerbs / sizeof kVerbs[0]; ++i) {
        if (![text hasPrefix:kVerbs[i]]) continue;
        NSString *rest = [text substringFromIndex:[kVerbs[i] length]];
        NSScanner *scanner = [NSScanner scannerWithString:rest];
        int count = 0;
        if (![scanner scanInt:&count]) return nil;
        NSString *tail = [rest substringFromIndex:[scanner scanLocation]];
        if (![tail hasPrefix:@" server"]) return nil;
        tail = [tail stringByReplacingOccurrencesOfString:@" server(s)" withString:@""];
        tail = [tail stringByReplacingOccurrencesOfString:@" (list full)"
                                               withString:@" (список заполнен)"];
        tail = [tail stringByReplacingOccurrencesOfString:@", skipped "
                                               withString:@", пропущено "];
        return [NSString stringWithFormat:@"%@ %d %@%@", kRussian[i], count,
                SenkoRussianPlural(count, @"сервер", @"сервера", @"серверов"), tail];
    }
    return nil;
}

static NSString *SenkoLocalizedDynamic(NSString *text) {
    if (![text length]) return text;

/* the daemon helper hint is appended after the reason, so the reason alone is
   what the table holds */
    NSString * const kickHint = @" (see /var/log/senko-kick.log)";
    if ([text hasSuffix:kickHint]) {
        NSString *head = [text substringToIndex:[text length] - [kickHint length]];
        return [SenkoLocalizedText(head)
                stringByAppendingString:@" (см. /var/log/senko-kick.log)"];
    }

    NSString *countReply = SenkoServerCountReply(text);
    if (countReply) return countReply;

    NSRange r = [text rangeOfString:@" server"];
    if (r.location != NSNotFound) {
        NSString *numberText = [text substringToIndex:r.location];
        NSInteger number = [numberText integerValue];
        if (SenkoAllDigits(numberText) && number >= 0) {
            NSString *tail = [text substringFromIndex:r.location + r.length];
            if ([tail hasPrefix:@"s"]) tail = [tail substringFromIndex:1];
            tail = [tail stringByReplacingOccurrencesOfString:@"until " withString:@"до "];
            if ([tail hasSuffix:@"expired"])
                tail = [tail stringByReplacingOccurrencesOfString:@"expired" withString:@"истёк"];
            return [NSString stringWithFormat:@"%ld %@%@", (long)number,
                    SenkoRussianPlural(number, @"сервер", @"сервера", @"серверов"), tail];
        }
    }
    r = [text rangeOfString:@" single config"];
    if (r.location != NSNotFound) {
        NSString *numberText = [text substringToIndex:r.location];
        NSInteger number = [numberText integerValue];
        if (SenkoAllDigits(numberText) && number >= 0) {
            NSString *tail = [text substringFromIndex:r.location + r.length];
            if ([tail hasPrefix:@"s"]) tail = [tail substringFromIndex:1];
            return [NSString stringWithFormat:@"%ld %@%@", (long)number,
                    SenkoRussianPlural(number, @"отдельная конфигурация", @"отдельные конфигурации", @"отдельных конфигураций"), tail];
        }
    }
    if ([text hasSuffix:@" ms"]) {
        NSString *number = [text substringToIndex:text.length - 3];
        if ([number integerValue] >= 0)
            return [NSString stringWithFormat:@"%@ мс", number];
    }
    if ([text hasPrefix:@"Installed "])
        return [NSString stringWithFormat:@"Установлено %@", [text substringFromIndex:10]];
    if ([text hasPrefix:@"Done: "])
        return [NSString stringWithFormat:@"Готово: %@", [text substringFromIndex:6]];
    if ([text hasPrefix:@"Package: "])
        return [NSString stringWithFormat:@"Пакет: %@", [text substringFromIndex:9]];
    if ([text hasPrefix:@"Version "])
        return [NSString stringWithFormat:@"Версия %@", [text substringFromIndex:8]];
    if ([text hasPrefix:@"cannot start senko-kick ("])
        return [NSString stringWithFormat:@"не удалось запустить senko-kick %@", [text substringFromIndex:24]];
    if ([text hasPrefix:@"daemon start failed ("])
        return [NSString stringWithFormat:@"не удалось запустить демон %@", [text substringFromIndex:20]];
    if ([text rangeOfString:@"    Dark"].location != NSNotFound)
        return [text stringByReplacingOccurrencesOfString:@"    Dark" withString:@"    Тёмная"];
    if ([text rangeOfString:@"    Light"].location != NSNotFound)
        return [text stringByReplacingOccurrencesOfString:@"    Light" withString:@"    Светлая"];
    if ([text hasPrefix:@"until "])
        return [NSString stringWithFormat:@"до %@", [text substringFromIndex:6]];
    return text;
}

static NSString *SenkoBaseText(NSString *text) {
    if (![text length]) return text;
    SenkoBuildTranslations();
    NSString *english = [gRussianToEnglish objectForKey:text];
    return english ? english : text;
}

NSString *SenkoLocalizedText(NSString *text) {
    if (![text length]) return text;
    NSString *english = SenkoBaseText(text);
    SenkoBuildTranslations();
    if (SenkoLanguageIsChinese()) {
        NSString *chinese = [gEnglishToChinese objectForKey:english];
        return chinese ? chinese : english;
    }
    if (!SenkoLanguageIsRussian()) return english;
    NSString *russian = [gEnglishToRussian objectForKey:english];
    return russian ? russian : SenkoLocalizedDynamic(english);
}

void SenkoSetLanguage(SenkoLanguage language) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    if (language < SenkoLanguageEnglish || language > SenkoLanguageChinese)
        language = SenkoLanguageEnglish;
    [defaults setInteger:language forKey:SENKO_LANGUAGE_KEY];
    [defaults synchronize];
    SenkoRelocalizeAllWindows();
    [[NSNotificationCenter defaultCenter] postNotificationName:SenkoLanguageDidChangeNotification object:nil];
}

static void SenkoSwizzle(Class cls, SEL original, SEL replacement) {
    Method a = class_getInstanceMethod(cls, original);
    Method b = class_getInstanceMethod(cls, replacement);
    if (a && b) method_exchangeImplementations(a, b);
}

static void SenkoRelocalizeView(UIView *view);

static void SenkoRelocalizeController(UIViewController *controller) {
    if (!controller) return;
    NSString *title = objc_getAssociatedObject(controller, &kSenkoRawControllerTitle);
    if (!title) title = SenkoBaseText(controller.title);
    if (title) controller.title = title;
    UINavigationItem *item = controller.navigationItem;
    NSString *navTitle = objc_getAssociatedObject(item, &kSenkoRawNavigationTitle);
    if (!navTitle) navTitle = SenkoBaseText(item.title);
    if (navTitle) item.title = navTitle;
    NSArray *bars = [NSArray arrayWithObjects:
                     item.leftBarButtonItem ? item.leftBarButtonItem : [NSNull null],
                     item.rightBarButtonItem ? item.rightBarButtonItem : [NSNull null], nil];
    for (id object in bars) {
        if ([object isKindOfClass:[UIBarButtonItem class]]) {
            UIBarButtonItem *bar = object;
            NSString *raw = objc_getAssociatedObject(bar, &kSenkoRawBarTitle);
            if (!raw) raw = SenkoBaseText(bar.title);
            if (raw) bar.title = SenkoLocalizedText(raw);
        }
    }
    SenkoRelocalizeView(controller.view);
    for (UIViewController *child in controller.childViewControllers)
        SenkoRelocalizeController(child);
    SenkoRelocalizeController(controller.presentedViewController);
}

static void SenkoRelocalizeView(UIView *view) {
    if ([view isKindOfClass:[UILabel class]]) {
        UILabel *label = (UILabel *)view;
        NSString *raw = objc_getAssociatedObject(label, &kSenkoRawText);
        if (!raw) raw = SenkoBaseText(label.text);
        if (raw) label.text = raw;
    }
    if ([view isKindOfClass:[UIButton class]]) {
        UIButton *button = (UIButton *)view;
        NSDictionary *titles = objc_getAssociatedObject(button, &kSenkoRawButtonTitles);
        for (NSNumber *stateNumber in titles) {
            NSString *raw = [titles objectForKey:stateNumber];
            [button setTitle:raw forState:[stateNumber unsignedIntegerValue]];
        }
    }
    if ([view isKindOfClass:[UITextField class]]) {
        UITextField *field = (UITextField *)view;
        NSString *raw = objc_getAssociatedObject(field, &kSenkoRawPlaceholder);
        if (!raw) raw = SenkoBaseText(field.placeholder);
        if (raw) field.placeholder = raw;
    }
    for (UIView *child in view.subviews)
        SenkoRelocalizeView(child);
}

void SenkoRelocalizeAllWindows(void) {
    if (!gLocalizationInstalled || ![UIApplication sharedApplication]) return;
    for (UIWindow *window in [UIApplication sharedApplication].windows) {
        SenkoRelocalizeController(window.rootViewController);
        SenkoRelocalizeView(window);
    }
}

@interface UILabel (SenkoLocalization)
- (void)senko_setText:(NSString *)text;
@end

@implementation UILabel (SenkoLocalization)
- (void)senko_setText:(NSString *)text {
    NSString *raw = SenkoBaseText(text);
    objc_setAssociatedObject(self, &kSenkoRawText, raw, OBJC_ASSOCIATION_COPY_NONATOMIC);
    [self senko_setText:SenkoLocalizedText(raw)];
}
@end

@interface UIButton (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title forState:(NSUInteger)state;
@end

@implementation UIButton (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title forState:(NSUInteger)state {
    NSString *raw = SenkoBaseText(title);
    NSMutableDictionary *titles = objc_getAssociatedObject(self, &kSenkoRawButtonTitles);
    if (!titles) {
        titles = [NSMutableDictionary dictionary];
        objc_setAssociatedObject(self, &kSenkoRawButtonTitles, titles, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    if (raw) [titles setObject:raw forKey:[NSNumber numberWithUnsignedInteger:state]];
    else [titles removeObjectForKey:[NSNumber numberWithUnsignedInteger:state]];
    [self senko_setTitle:SenkoLocalizedText(raw) forState:state];
}
@end

@interface UITextField (SenkoLocalization)
- (void)senko_setPlaceholder:(NSString *)placeholder;
@end

@implementation UITextField (SenkoLocalization)
- (void)senko_setPlaceholder:(NSString *)placeholder {
    NSString *raw = SenkoBaseText(placeholder);
    objc_setAssociatedObject(self, &kSenkoRawPlaceholder, raw, OBJC_ASSOCIATION_COPY_NONATOMIC);
    [self senko_setPlaceholder:SenkoLocalizedText(raw)];
}
@end

@interface UIViewController (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title;
@end

@implementation UIViewController (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title {
    NSString *raw = SenkoBaseText(title);
    objc_setAssociatedObject(self, &kSenkoRawControllerTitle, raw, OBJC_ASSOCIATION_COPY_NONATOMIC);
    [self senko_setTitle:SenkoLocalizedText(raw)];
}
@end

@interface UINavigationItem (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title;
@end

@implementation UINavigationItem (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title {
    NSString *raw = SenkoBaseText(title);
    objc_setAssociatedObject(self, &kSenkoRawNavigationTitle, raw, OBJC_ASSOCIATION_COPY_NONATOMIC);
    [self senko_setTitle:SenkoLocalizedText(raw)];
}
@end

@interface UIBarButtonItem (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title;
- (id)senko_initWithBarButtonSystemItem:(UIBarButtonSystemItem)item
                                target:(id)target
                                action:(SEL)action;
@end

@implementation UIBarButtonItem (SenkoLocalization)
- (void)senko_setTitle:(NSString *)title {
    NSString *raw = SenkoBaseText(title);
    objc_setAssociatedObject(self, &kSenkoRawBarTitle, raw, OBJC_ASSOCIATION_COPY_NONATOMIC);
    [self senko_setTitle:SenkoLocalizedText(raw)];
}

/* a system item keeps the width uikit measured for its own english word, so
   putting a longer one on it makes the control grow and then clip the word in
   the middle ("Го...во"). the word bearing items are built as titled items
   instead, which measure their own text. Add and Refresh are glyphs and are
   left alone: a title on those would replace the icon */
- (id)senko_initWithBarButtonSystemItem:(UIBarButtonSystemItem)item
                                target:(id)target
                                action:(SEL)action {
    NSString *raw = nil;
    UIBarButtonItemStyle style = UIBarButtonItemStylePlain;
    switch (item) {
        case UIBarButtonSystemItemDone:
            raw = @"Done"; style = UIBarButtonItemStyleDone; break;
        case UIBarButtonSystemItemSave:
            raw = @"Save"; style = UIBarButtonItemStyleDone; break;
        case UIBarButtonSystemItemCancel:
            raw = @"Cancel"; break;
        default: break;
    }
    if (!raw)
        return [self senko_initWithBarButtonSystemItem:item target:target action:action];
/* setTitle: is swizzled, so the raw word is recorded by the initializer and a
   later language change relocalizes it through the same path */
    return [self initWithTitle:SenkoLocalizedText(raw) style:style
                        target:target action:action];
}
@end

void SenkoLocalizationInstall(void) {
    if (gLocalizationInstalled) return;
    SenkoBuildTranslations();
    SenkoSwizzle([UILabel class], @selector(setText:), @selector(senko_setText:));
    SenkoSwizzle([UIButton class], @selector(setTitle:forState:), @selector(senko_setTitle:forState:));
    SenkoSwizzle([UITextField class], @selector(setPlaceholder:), @selector(senko_setPlaceholder:));
    SenkoSwizzle([UIViewController class], @selector(setTitle:), @selector(senko_setTitle:));
    SenkoSwizzle([UINavigationItem class], @selector(setTitle:), @selector(senko_setTitle:));
    SenkoSwizzle([UIBarButtonItem class], @selector(setTitle:), @selector(senko_setTitle:));
    SenkoSwizzle([UIBarButtonItem class], @selector(initWithBarButtonSystemItem:target:action:), @selector(senko_initWithBarButtonSystemItem:target:action:));
    gLocalizationInstalled = YES;
}
