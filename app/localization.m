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
    SenkoAddTranslation(@"Amnezia/VLESS Client", @"Клиент Amnezia/VLESS");
    SenkoAddTranslation(@"Model", @"Модель");
    SenkoAddTranslation(@"iOS version", @"Версия iOS");
    SenkoAddTranslation(@"Architecture", @"Архитектура");
    SenkoAddTranslation(@"TLS mode", @"Режим TLS");
    SenkoAddTranslation(@"How it works", @"Как это работает");
    SenkoAddTranslation(@"Apps and system traffic use the selected profile. Routing needs root, and a jailbreak already provides it.", @"Приложения и системный трафик идут через выбранный профиль. Для маршрутизации нужен root, и джейлбрейк его уже даёт.");
    SenkoAddTranslation(@"Transports", @"Транспорты");
    SenkoAddTranslation(@"Compatibility", @"Совместимость");
    SenkoAddTranslation(@"Token-authenticated control socket · subscription SSRF protection · secret redaction · real transport checks.", @"Control socket с токеном · защита подписок от SSRF · скрытие секретов в логах · настоящая проверка транспорта.");
    SenkoAddTranslation(@"Links", @"Ссылки");
    SenkoAddTranslation(@"Credits", @"Благодарности");
    SenkoAddTranslation(@"Special thanks", @"Особая благодарность");
    SenkoAddTranslation(@"Sponsors", @"Спонсоры");
    SenkoAddTranslation(@"Testers", @"Тестировали");
    SenkoAddTranslation(@"Emoji artwork", @"Эмодзи");
    SenkoAddTranslation(@"Twemoji by Twitter, Inc. and contributors (CC BY 4.0)", @"Twemoji от Twitter, Inc. и участников проекта (CC BY 4.0)");
    SenkoAddTranslation(@"system TLS, the compatibility hook is not injected", @"системный TLS, compatibility hook не внедряется");
    SenkoAddTranslation(@"external tlsfix present, senkotlsfix hooks stay off", @"внешний tlsfix найден, хуки senkotlsfix отключены");
    SenkoAddTranslation(@"senkotlsfix, Safari TLS 1.3 when MobileSubstrate is installed", @"senkotlsfix для TLS 1.3 в Safari при установленном MobileSubstrate");
    SenkoAddTranslation(@"PROTOCOL", @"ПРОТОКОЛ");
    SenkoAddTranslation(@"PASSWORD", @"ПАРОЛЬ");
    SenkoAddTranslation(@"CIPHER", @"ШИФР");
    SenkoAddTranslation(@"Shadowsocks", @"Shadowsocks");
    SenkoAddTranslation(@"Trojan", @"Trojan");
    SenkoAddTranslation(@"Add", @"Добавить");
    SenkoAddTranslation(@"Available servers: %lu", @"Доступно серверов: %lu");
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
    SenkoAddTranslation(@"Every %@", @"Каждые %@");
    SenkoAddTranslation(@"The daemon runs these on its own, with the app closed.",
                        @"Демон выполняет это сам, при закрытом приложении.");
    SenkoAddTranslation(@"The daemon is not answering, so these cannot be read or changed.",
                        @"Демон не отвечает, прочитать и изменить их нельзя.");
    SenkoAddTranslation(@"Daemon is unreachable", @"Демон не отвечает");
    SenkoAddTranslation(@"DEVELOPER", @"РАЗРАБОТЧИКУ");
    SenkoAddTranslation(@"Developer settings", @"Настройки разработчика");
    SenkoAddTranslation(@"Developer settings are now visible in Settings. Tap the section heading five times to hide them again.", @"Настройки разработчика теперь видны в настройках. Чтобы снова скрыть их, пять раз нажмите на заголовок раздела.");
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
    SenkoAddTranslation(@"Senko-core eligible", @"senko-core подходит");
    SenkoAddTranslation(@"Applied on the next connect.", @"Применится при следующем подключении.");
    SenkoAddTranslation(@"Auto", @"Авто");
    SenkoAddTranslation(@"Backend, tunnel, ports, DNS, rules", @"Бэкенд, туннель, порты, DNS, правила");
    SenkoAddTranslation(@"Backend, listeners, trace", @"Бэкенд, слушатели, трассировка");
    SenkoAddTranslation(@"utun tunnel", @"Туннель utun");
    SenkoAddTranslation(@"Forget direct addresses", @"Забыть прямые адреса");
    SenkoAddTranslation(@"Tunnel routes", @"Маршруты туннеля");
    SenkoAddTranslation(@"Show tunnel routes", @"Показать маршруты туннеля");
    SenkoAddTranslation(@"Tunnel DNS", @"DNS туннеля");
    SenkoAddTranslation(@"Tunnel counters", @"Счётчики туннеля");
    SenkoAddTranslation(@"TCP flows", @"TCP-потоки");
    SenkoAddTranslation(@"TCP traffic", @"TCP-трафик");
    SenkoAddTranslation(@"Last TCP error", @"Последняя ошибка TCP");
    SenkoAddTranslation(@"UDP datagrams", @"UDP-датаграммы");
    SenkoAddTranslation(@"Last UDP error", @"Последняя ошибка UDP");
    SenkoAddTranslation(@"Packets", @"Пакеты");
    SenkoAddTranslation(@"Dropped packets", @"Отброшенные пакеты");
    SenkoAddTranslation(@"DNS queries", @"DNS-запросы");
    SenkoAddTranslation(@"Rule verdicts", @"Решения правил");
    SenkoAddTranslation(@"Direct addresses", @"Прямые адреса");
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
    SenkoAddTranslation(@"Reset settings", @"Сбросить настройки");
    SenkoAddTranslation(@"Run", @"Запустить");
    SenkoAddTranslation(@"Safe mode next launch", @"Safe mode при следующем запуске");
    SenkoAddTranslation(@"Session trace", @"Трассировка сессий");
    SenkoAddTranslation(@"SOCKS on 0.0.0.0", @"SOCKS на 0.0.0.0");
    SenkoAddTranslation(@"Staged probes and the tunnel routes", @"Пробы по стадиям и маршруты туннеля");
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
    SenkoAddTranslation(@"Free memory", @"Свободная память");
    SenkoAddTranslation(@"Senko resident", @"Senko занимает");
    SenkoAddTranslation(@"Device uptime", @"Устройство работает");
    SenkoAddTranslation(@"Device", @"Устройство");
    SenkoAddTranslation(@"Battery", @"Батарея");
    SenkoAddTranslation(@"Senko-core usable", @"senko-core подходит");
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
    SenkoAddTranslation(@"LISTENERS AND DNS", @"СЛУШАТЕЛИ И DNS");
    SenkoAddTranslation(@"SUBSCRIPTIONS", @"ПОДПИСКИ");
    SenkoAddTranslation(@"DIAGNOSTICS", @"ДИАГНОСТИКА");
    SenkoAddTranslation(@"Backend", @"Бэкенд");
    SenkoAddTranslation(@"Senko-core", @"senko-core");
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
    SenkoAddTranslation(@"Bypass table", @"Таблица обхода");
    SenkoAddTranslation(@"Redirect port", @"Порт перенаправления");
    SenkoAddTranslation(@"DNS port", @"Порт DNS");
    SenkoAddTranslation(@"Live connections", @"Живых соединений");
    SenkoAddTranslation(@"AmneziaWG profile", @"Профиль AmneziaWG");
    SenkoAddTranslation(@"AmneziaWG route", @"Маршрут AmneziaWG");
    SenkoAddTranslation(@"AmneziaWG DNS", @"DNS AmneziaWG");
    SenkoAddTranslation(@"AmneziaWG link", @"Канал AmneziaWG");
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
    SenkoAddTranslation(@"senkod is missing: reinstall the package",
                        @"senkod отсутствует: переустановите пакет");
    SenkoAddTranslation(@"the senkod launch daemon is missing: reinstall the package",
                        @"нет задания launchd для senkod: переустановите пакет");
    SenkoAddTranslation(@"senkod is not running and its log is empty",
                        @"senkod не запущен, его лог пуст");
    SenkoAddTranslation(@"This device received a link back to the same address instead of a subscription feed. Check device access and ask the provider for the feed URL.",
                        @"Устройство получило ссылку на тот же адрес вместо списка серверов. Проверьте доступ устройства к подписке и запросите у провайдера ссылку на список.");
    SenkoAddTranslation(@"The Happ crypt5 bundle on this page could not be opened. It is either damaged or sealed with a key this build does not carry.",
                        @"Бандл Happ crypt5 на этой странице не открылся: он либо повреждён, либо запечатан ключом, которого нет в этой сборке.");
    SenkoAddTranslation(@"This address opens a web page instead of a subscription feed. Copy the subscription link the page offers, not the page address.",
                        @"По этому адресу открывается веб-страница, а не подписка. Скопируйте ссылку на подписку, которую предлагает страница, а не адрес самой страницы.");
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
    SenkoAddTranslation(@"The tunnel could not start. Open System Logs to see whether utun, routes, or the bundled core failed.", @"Не удалось запустить туннель. Откройте системные логи: там указано, что именно не сработало, utun, маршруты или встроенное ядро.");
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
    SenkoAddTranslation(@"disconnect to edit", @"отключитесь для редактирования");
    SenkoAddTranslation(@"disconnect to remove", @"отключитесь для удаления");
    SenkoAddTranslation(@"disconnect to reorder", @"отключитесь для изменения порядка");
    SenkoAddTranslation(@"disconnect to switch", @"отключитесь для переключения");
    SenkoAddTranslation(@"disconnect to switch backend", @"отключитесь для смены режима");
    SenkoAddTranslation(@"fetch failed: daemon offline", @"не удалось получить данные: демон недоступен");
    SenkoAddTranslation(@"fetching subscription...", @"получение подписки...");
    SenkoAddTranslation(@"file import failed", @"не удалось импортировать файл");
    SenkoAddTranslation(@"folder", @"папка");
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
    SenkoAddTranslation(@"The connect hook could not start. Check that senkotlsfix is installed, or pick another backend.", @"Connect-хук не запустился. Проверьте, что senkotlsfix установлен, или выберите другой бэкенд.");
    SenkoAddTranslation(@"The tunnel could not start. Open System Logs to see whether utun, routes, or the server failed.", @"Туннель не запустился. Откройте «Системные логи»: там видно, что отказало — utun, маршруты или сервер.");
    SenkoAddTranslation(@"The tunnel stopped right after it started. Open System Logs for the reason.", @"Туннель остановился сразу после запуска. Причина — в «Системных логах».");
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
    SenkoAddTranslation(@"Download", @"Загрузка");
    SenkoAddTranslation(@"Upload", @"Отдача");
    SenkoAddTranslation(@"Statistics", @"Статистика");
    SenkoAddTranslation(@"Choose a server", @"Выбор сервера");
    SenkoAddTranslation(@"Search a country or a city", @"Поиск страны или города");
    SenkoAddTranslation(@"Fastest server", @"Лучший сервер");
    SenkoAddTranslation(@"Tap to connect", @"Нажми, чтобы подключиться");
    SenkoAddTranslation(@"Looking for the fastest server", @"Ищу самый быстрый сервер");
    SenkoAddTranslation(@"no server answered the check", @"ни один сервер не ответил на проверку");
    SenkoAddTranslation(@"No servers", @"Нет серверов");
    SenkoAddTranslation(@"Add a subscription or a server", @"Добавь подписку или сервер");
    SenkoAddTranslation(@"Tap to open the list", @"Нажми, чтобы открыть список");
    SenkoAddTranslation(@"%d ms", @"%d мс");
    SenkoAddTranslation(@"Mbit/s", @"Мбит/с");
    SenkoAddTranslation(@"Kbit/s", @"Кбит/с");
    SenkoAddTranslation(@"bit/s", @"бит/с");
    SenkoAddTranslation(@"Gbit/s", @"Гбит/с");
    SenkoAddTranslation(@"Done reordering", @"Закончить перестановку");
    SenkoAddTranslation(@"Reorder manual servers", @"Переставить свои серверы");
    SenkoAddTranslation(@"Session", @"Сеанс");
    SenkoAddTranslation(@"Subscriptions", @"Подписки");
    SenkoAddTranslation(@"Status", @"Состояние");
    SenkoAddTranslation(@"Uptime", @"Время работы");
    SenkoAddTranslation(@"Download speed", @"Скорость загрузки");
    SenkoAddTranslation(@"Upload speed", @"Скорость отдачи");
    SenkoAddTranslation(@"%@ of %@", @"%@ из %@");
    SenkoAddTranslation(@"Main", @"Основные");
    SenkoAddTranslation(@"App", @"Приложение");
    SenkoAddTranslation(@"Advanced", @"Дополнительно");
    SenkoAddTranslation(@"Quick connect", @"Быстрое подключение");
    SenkoAddTranslation(@"Theme", @"Тема");
    SenkoAddTranslation(@"Backup", @"Резервная копия");
    SenkoAddTranslation(@"DNS", @"DNS");
    SenkoAddTranslation(@"Split tunneling", @"Раздельное туннелирование");
    SenkoAddTranslation(@"Real delay", @"Реальная задержка");

/* common screen text: Chinese is kept here instead of relying on system
   strings, because the app runs on iOS 5 where no localization bundle exists */
    SenkoAddChineseTranslation(@"About", @"关于");
    SenkoAddChineseTranslation(@"Amnezia/VLESS Client", @"Amnezia/VLESS 客户端");
    SenkoAddChineseTranslation(@"Model", @"型号");
    SenkoAddChineseTranslation(@"iOS version", @"iOS 版本");
    SenkoAddChineseTranslation(@"Architecture", @"架构");
    SenkoAddChineseTranslation(@"TLS mode", @"TLS 模式");
    SenkoAddChineseTranslation(@"How it works", @"工作方式");
    SenkoAddChineseTranslation(@"Apps and system traffic use the selected profile. Routing needs root, and a jailbreak already provides it.", @"应用和系统流量使用选中的配置。路由需要 root，越狱已经提供了权限。");
    SenkoAddChineseTranslation(@"Transports", @"传输协议");
    SenkoAddChineseTranslation(@"Compatibility", @"兼容性");
    SenkoAddChineseTranslation(@"Token-authenticated control socket · subscription SSRF protection · secret redaction · real transport checks.", @"带令牌认证的控制 socket · 防止订阅 SSRF · 日志隐藏密钥 · 真实传输检查。");
    SenkoAddChineseTranslation(@"Links", @"链接");
    SenkoAddChineseTranslation(@"Credits", @"致谢");
    SenkoAddChineseTranslation(@"Special thanks", @"特别感谢");
    SenkoAddChineseTranslation(@"Sponsors", @"赞助者");
    SenkoAddChineseTranslation(@"Testers", @"测试者");
    SenkoAddChineseTranslation(@"Emoji artwork", @"表情图案");
    SenkoAddChineseTranslation(@"Twemoji by Twitter, Inc. and contributors (CC BY 4.0)", @"Twitter, Inc. 及贡献者的 Twemoji（CC BY 4.0）");
    SenkoAddChineseTranslation(@"system TLS, the compatibility hook is not injected", @"系统 TLS，不注入兼容性 hook");
    SenkoAddChineseTranslation(@"external tlsfix present, senkotlsfix hooks stay off", @"检测到外部 tlsfix，保持 senkotlsfix hook 关闭");
    SenkoAddChineseTranslation(@"senkotlsfix, Safari TLS 1.3 when MobileSubstrate is installed", @"senkotlsfix，用于 MobileSubstrate 下 Safari TLS 1.3");
    SenkoAddChineseTranslation(@"Settings", @"设置");
    SenkoAddChineseTranslation(@"Language", @"语言");
    SenkoAddChineseTranslation(@"English", @"English");
    SenkoAddChineseTranslation(@"Russian", @"Русский");
    SenkoAddChineseTranslation(@"Chinese", @"中文");
    SenkoAddChineseTranslation(@"GENERAL", @"常规");
    SenkoAddChineseTranslation(@"APP", @"应用");
    SenkoAddChineseTranslation(@"DEVELOPER", @"开发者");
    SenkoAddChineseTranslation(@"Developer settings", @"开发者设置");
    SenkoAddChineseTranslation(@"Developer settings are now visible in Settings. Tap the section heading five times to hide them again.", @"开发者设置现已显示在设置中。点击该区域标题五次即可再次隐藏。");
    SenkoAddChineseTranslation(@"Server", @"服务器");
    SenkoAddChineseTranslation(@"SERVER", @"服务器");
    SenkoAddChineseTranslation(@"Add", @"添加");
    SenkoAddChineseTranslation(@"Available servers: %lu", @"可用服务器：%lu");
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
    SenkoAddChineseTranslation(@"Every %@", @"每 %@");
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
    SenkoAddChineseTranslation(@"Style", @"风格");
    SenkoAddChineseTranslation(@"Custom", @"自定义");
    SenkoAddChineseTranslation(@"Appearance", @"外观");
    SenkoAddChineseTranslation(@"Dark / Light applies to the selected style. Choice is stored on device.", @"深色和浅色模式应用于所选风格。设置保存在设备上。");
    SenkoAddChineseTranslation(@"Senko-Miside is Dark only: pattern wallpaper and candy heart ON.", @"Senko-Miside 仅支持深色模式：图案壁纸和糖果心形按钮。");
    SenkoAddChineseTranslation(@"Senko-Boykisser: pink paper or rose ink, with falling boykissers on the home screen.", @"Senko-Boykisser：粉色纸张或玫瑰色墨迹，主屏幕上有飘落的图案。");
    SenkoAddChineseTranslation(@"Senko-Aero is Light only: sky wallpaper and floating gloss bubbles.", @"Senko-Aero 仅支持浅色模式：天空壁纸和漂浮的光泽气泡。");
    SenkoAddChineseTranslation(@"Play a short ouch on every button tap.", @"每次点击按钮时播放短音效。");
    SenkoAddChineseTranslation(@"Play a short meow on every button tap.", @"每次点击按钮时播放短猫叫声。");
    SenkoAddChineseTranslation(@"Dark", @"深色");
    SenkoAddChineseTranslation(@"Light", @"浅色");
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
    SenkoAddChineseTranslation(@"Senko", @"Senko");
    SenkoAddChineseTranslation(@"No server in the catalog", @"目录中没有服务器");
    SenkoAddChineseTranslation(@"No servers in the catalog.", @"目录中没有服务器。");
    SenkoAddChineseTranslation(@"Unknown error", @"未知错误");
    SenkoAddChineseTranslation(@"The report is on the clipboard.", @"报告已复制到剪贴板。");
    SenkoAddChineseTranslation(@"Copied to the clipboard.", @"已复制到剪贴板。");
    SenkoAddChineseTranslation(@"Download", @"下载");
    SenkoAddChineseTranslation(@"Upload", @"上传");
    SenkoAddChineseTranslation(@"Statistics", @"统计");
    SenkoAddChineseTranslation(@"Choose a server", @"选择服务器");
    SenkoAddChineseTranslation(@"Search a country or a city", @"搜索国家或城市");
    SenkoAddChineseTranslation(@"Auto", @"自动");
    SenkoAddChineseTranslation(@"All", @"全部");
    SenkoAddChineseTranslation(@"Fastest server", @"最快的服务器");
    SenkoAddChineseTranslation(@"Tap to connect", @"点击连接");
    SenkoAddChineseTranslation(@"Looking for the fastest server", @"正在寻找最快的服务器");
    SenkoAddChineseTranslation(@"no server answered the check", @"没有服务器响应检测");
    SenkoAddChineseTranslation(@"AmneziaWG profile", @"AmneziaWG 配置");
    SenkoAddChineseTranslation(@"No servers", @"没有服务器");
    SenkoAddChineseTranslation(@"Add a subscription or a server", @"添加订阅或服务器");
    SenkoAddChineseTranslation(@"Tap to open the list", @"点击打开列表");
    SenkoAddChineseTranslation(@"%d ms", @"%d 毫秒");
    SenkoAddChineseTranslation(@"Mbit/s", @"Mbit/s");
    SenkoAddChineseTranslation(@"Kbit/s", @"Kbit/s");
    SenkoAddChineseTranslation(@"Done reordering", @"完成排序");
    SenkoAddChineseTranslation(@"Reorder manual servers", @"调整手动服务器顺序");
    SenkoAddChineseTranslation(@"Session", @"会话");
    SenkoAddChineseTranslation(@"Subscriptions", @"订阅");
    SenkoAddChineseTranslation(@"Status", @"状态");
    SenkoAddChineseTranslation(@"Uptime", @"运行时间");
    SenkoAddChineseTranslation(@"Download speed", @"下载速度");
    SenkoAddChineseTranslation(@"Upload speed", @"上传速度");
    SenkoAddChineseTranslation(@"%@ of %@", @"%@ / %@");
    SenkoAddChineseTranslation(@"expired", @"已过期");
    SenkoAddChineseTranslation(@"Main", @"常规");
    SenkoAddChineseTranslation(@"App", @"应用");
    SenkoAddChineseTranslation(@"Advanced", @"高级");
    SenkoAddChineseTranslation(@"Quick connect", @"快速连接");
    SenkoAddChineseTranslation(@"Theme", @"主题");
    SenkoAddChineseTranslation(@"Backup", @"备份");
    SenkoAddChineseTranslation(@"DNS", @"DNS");
    SenkoAddChineseTranslation(@"Split tunneling", @"分流");
    SenkoAddChineseTranslation(@"Real delay", @"真实延迟");
    SenkoAddChineseTranslation(@"Local DNS port", @"本地 DNS 端口");
    SenkoAddChineseTranslation(@"SOCKS port", @"SOCKS 端口");
    SenkoAddChineseTranslation(@"unreachable", @"无法访问");
    SenkoAddChineseTranslation(@"%d more", @"还有 %d 项");
    SenkoAddChineseTranslation(@"A port number between 1 and 65535", @"请输入 1 到 65535 之间的端口号");
    SenkoAddChineseTranslation(@"Active tunnel", @"当前隧道");
    SenkoAddChineseTranslation(@"Add dark variant", @"添加深色版本");
    SenkoAddChineseTranslation(@"AmneziaWG: check ping", @"AmneziaWG：检查延迟");
    SenkoAddChineseTranslation(@"AmneziaWG: edit details", @"AmneziaWG：编辑详情");
    SenkoAddChineseTranslation(@"AmneziaWG: refresh", @"AmneziaWG：刷新");
    SenkoAddChineseTranslation(@"AmneziaWG: remove profile", @"AmneziaWG：移除配置");
    SenkoAddChineseTranslation(@"An IPv4 address, for example 1.1.1.1", @"请输入 IPv4 地址，例如 1.1.1.1");
    SenkoAddChineseTranslation(@"Applied on the next connect.", @"下次连接时生效。");
    SenkoAddChineseTranslation(@"Asking the daemon...", @"正在查询守护进程…");
    SenkoAddChineseTranslation(@"Backend", @"后端");
    SenkoAddChineseTranslation(@"Backend, listeners, trace", @"后端、监听器、跟踪");
    SenkoAddChineseTranslation(@"Backend, tunnel, ports, DNS, rules", @"后端、隧道、端口、DNS、规则");
    SenkoAddChineseTranslation(@"Backup export failed", @"导出备份失败");
    SenkoAddChineseTranslation(@"Backup restore failed", @"恢复备份失败");
    SenkoAddChineseTranslation(@"Blocked answers", @"已阻止的响应");
    SenkoAddChineseTranslation(@"CHECK", @"检查");
    SenkoAddChineseTranslation(@"CIPHER", @"加密方式");
    SenkoAddChineseTranslation(@"Catalog and rules are kept.", @"服务器目录和规则已保留。");
    SenkoAddChineseTranslation(@"Catalog is kept.", @"服务器目录已保留。");
    SenkoAddChineseTranslation(@"Checks", @"检查");
    SenkoAddChineseTranslation(@"Classic", @"经典");
    SenkoAddChineseTranslation(@"Clear", @"清除");
    SenkoAddChineseTranslation(@"Connect hook only", @"仅连接钩子");
    SenkoAddChineseTranslation(@"Console", @"控制台");
    SenkoAddChineseTranslation(@"Copied in full.", @"已完整复制。");
    SenkoAddChineseTranslation(@"Copy current theme", @"复制当前主题");
    SenkoAddChineseTranslation(@"Corners", @"圆角");
    SenkoAddChineseTranslation(@"Could not create theme", @"无法创建主题");
    SenkoAddChineseTranslation(@"Could not stage backup", @"无法暂存备份");
    SenkoAddChineseTranslation(@"Crash and launch log", @"崩溃与启动日志");
    SenkoAddChineseTranslation(@"Crash and launch report", @"崩溃与启动报告");
    SenkoAddChineseTranslation(@"Crash log, safe mode, device id, bundle", @"崩溃日志、安全模式、设备 ID、诊断包");
    SenkoAddChineseTranslation(@"DARK COLORS", @"深色配色");
    SenkoAddChineseTranslation(@"DEVICE ID", @"设备 ID");
    SenkoAddChineseTranslation(@"DIAGNOSTICS", @"诊断");
    SenkoAddChineseTranslation(@"Daemon is unreachable", @"无法连接守护进程");
    SenkoAddChineseTranslation(@"Delete all rules", @"删除所有规则");
    SenkoAddChineseTranslation(@"Delete theme", @"删除主题");
    SenkoAddChineseTranslation(@"Delete theme?", @"删除主题？");
    SenkoAddChineseTranslation(@"Device ID (tap to copy)", @"设备 ID（点击复制）");
    SenkoAddChineseTranslation(@"Device ID copied", @"设备 ID 已复制");
    SenkoAddChineseTranslation(@"Device id", @"设备 ID");
    SenkoAddChineseTranslation(@"Diagnostics bundle", @"诊断包");
    SenkoAddChineseTranslation(@"Done.", @"已完成。");
    SenkoAddChineseTranslation(@"Duplicates", @"重复项");
    SenkoAddChineseTranslation(@"Edit details", @"编辑详情");
    SenkoAddChineseTranslation(@"Edit server", @"编辑服务器");
    SenkoAddChineseTranslation(@"Editing", @"正在编辑");
    SenkoAddChineseTranslation(@"Empty manual list", @"清空手动列表");
    SenkoAddChineseTranslation(@"Enter", @"输入");
    SenkoAddChineseTranslation(@"Export debug bundle", @"导出调试包");
    SenkoAddChineseTranslation(@"Export failed", @"导出失败");
    SenkoAddChineseTranslation(@"Export to Documents", @"导出到 Documents");
    SenkoAddChineseTranslation(@"FINGERPRINT", @"指纹");
    SenkoAddChineseTranslation(@"FIREWALL", @"防火墙");
    SenkoAddChineseTranslation(@"FLOW", @"流控");
    SenkoAddChineseTranslation(@"FORCE", @"强制");
    SenkoAddChineseTranslation(@"FPS overlay", @"FPS 浮窗");
    SenkoAddChineseTranslation(@"Failed launches", @"启动失败次数");
    SenkoAddChineseTranslation(@"Flat", @"扁平");
    SenkoAddChineseTranslation(@"Flush", @"清空");
    SenkoAddChineseTranslation(@"Flush DNS cache", @"清空 DNS 缓存");
    SenkoAddChineseTranslation(@"For example 10.0.0.0/8", @"例如 10.0.0.0/8");
    SenkoAddChineseTranslation(@"For example example.com", @"例如 example.com");
    SenkoAddChineseTranslation(@"For example googlevideo", @"例如 googlevideo");
    SenkoAddChineseTranslation(@"Force", @"强制");
    SenkoAddChineseTranslation(@"Forget direct addresses", @"清除直连地址");
    SenkoAddChineseTranslation(@"Glass", @"玻璃");
    SenkoAddChineseTranslation(@"HWID requires a Cookie request header", @"HWID 需要 Cookie 请求头");
    SenkoAddChineseTranslation(@"Header: value", @"请求头：值");
    SenkoAddChineseTranslation(@"Ignore device gating", @"忽略设备限制");
    SenkoAddChineseTranslation(@"Import", @"导入");
    SenkoAddChineseTranslation(@"Import failed", @"导入失败");
    SenkoAddChineseTranslation(@"Import from Documents", @"从 Documents 导入");
    SenkoAddChineseTranslation(@"Import theme", @"导入主题");
    SenkoAddChineseTranslation(@"Issue", @"问题");
    SenkoAddChineseTranslation(@"LAUNCH", @"启动");
    SenkoAddChineseTranslation(@"LIGHT COLORS", @"浅色配色");
    SenkoAddChineseTranslation(@"LISTENERS AND DNS", @"监听器与 DNS");
    SenkoAddChineseTranslation(@"Leave safe mode", @"退出安全模式");
    SenkoAddChineseTranslation(@"Local proxy", @"本地代理");
    SenkoAddChineseTranslation(@"Long names", @"长名称");
    SenkoAddChineseTranslation(@"Long names, duplicates and an empty manual list", @"长名称、重复项和空的手动列表");
    SenkoAddChineseTranslation(@"Look", @"外观");
    SenkoAddChineseTranslation(@"Mode", @"模式");
    SenkoAddChineseTranslation(@"Name", @"名称");
    SenkoAddChineseTranslation(@"New device id", @"生成新设备 ID");
    SenkoAddChineseTranslation(@"New theme", @"新建主题");
    SenkoAddChineseTranslation(@"No app fault report", @"没有应用崩溃报告");
    SenkoAddChineseTranslation(@"No rules: everything goes through the tunnel. Add one with the plus button.", @"没有规则：所有流量都经过隧道。点击加号添加规则。");
    SenkoAddChineseTranslation(@"None", @"无");
    SenkoAddChineseTranslation(@"Not a senko backup", @"不是 Senko 备份文件");
    SenkoAddChineseTranslation(@"Off from the next launch.", @"下次启动时关闭。");
    SenkoAddChineseTranslation(@"Only manually added servers are removed.", @"只会删除手动添加的服务器。");
    SenkoAddChineseTranslation(@"PATH", @"路径");
    SenkoAddChineseTranslation(@"PROTOCOL", @"协议");
    SenkoAddChineseTranslation(@"Profile handshake", @"配置握手");
    SenkoAddChineseTranslation(@"REMOVE", @"移除");
    SenkoAddChineseTranslation(@"Reading the rules from the daemon...", @"正在从守护进程读取规则…");
    SenkoAddChineseTranslation(@"Refreshing subscriptions", @"正在刷新订阅");
    SenkoAddChineseTranslation(@"Replace", @"替换");
    SenkoAddChineseTranslation(@"Rescue", @"恢复");
    SenkoAddChineseTranslation(@"Reset", @"重置");
    SenkoAddChineseTranslation(@"Reset settings", @"重置设置");
    SenkoAddChineseTranslation(@"Restore configuration", @"恢复配置");
    SenkoAddChineseTranslation(@"Result", @"结果");
    SenkoAddChineseTranslation(@"Run", @"运行");
    SenkoAddChineseTranslation(@"Running...", @"运行中…");
    SenkoAddChineseTranslation(@"SOCKS on 0.0.0.0", @"在 0.0.0.0 上监听 SOCKS");
    SenkoAddChineseTranslation(@"STAGES", @"阶段");
    SenkoAddChineseTranslation(@"STYLE", @"风格");
    SenkoAddChineseTranslation(@"SUBSCRIPTIONS", @"订阅");
    SenkoAddChineseTranslation(@"Safe mode", @"安全模式");
    SenkoAddChineseTranslation(@"Safe mode next launch", @"下次启动进入安全模式");
    SenkoAddChineseTranslation(@"Saved to Documents/senko-backup.senko", @"已保存到 Documents/senko-backup.senko");
    SenkoAddChineseTranslation(@"Send HWID in Cookie", @"在 Cookie 中发送 HWID");
    SenkoAddChineseTranslation(@"Senko-core", @"Senko-core");
    SenkoAddChineseTranslation(@"Session trace", @"会话跟踪");
    SenkoAddChineseTranslation(@"Show tunnel routes", @"显示隧道路由");
    SenkoAddChineseTranslation(@"Staged probes and the tunnel routes", @"分阶段检测和隧道路由");
    SenkoAddChineseTranslation(@"Stock theme, no glass, no decor. Your theme stays on disk.", @"使用默认主题，不显示玻璃与装饰效果。原主题仍保存在设备上。");
    SenkoAddChineseTranslation(@"TCP to the node", @"连接节点的 TCP");
    SenkoAddChineseTranslation(@"Talk to the control socket without ssh", @"无需 SSH 即可访问控制套接字");
    SenkoAddChineseTranslation(@"Test fixtures", @"测试数据");
    SenkoAddChineseTranslation(@"The custom theme limit is reached.", @"自定义主题数量已达上限。");
    SenkoAddChineseTranslation(@"The daemon is not answering, so these cannot be read or changed.", @"守护进程没有响应，因此无法读取或更改这些设置。");
    SenkoAddChineseTranslation(@"The next launch runs in safe mode.", @"下次启动将进入安全模式。");
    SenkoAddChineseTranslation(@"The old id is gone for good.", @"旧设备 ID 已永久删除。");
    SenkoAddChineseTranslation(@"The subscription refused this device.", @"订阅拒绝此设备。");
    SenkoAddChineseTranslation(@"Theme exported", @"主题已导出");
    SenkoAddChineseTranslation(@"Theme imported", @"主题已导入");
    SenkoAddChineseTranslation(@"Theme name", @"主题名称");
    SenkoAddChineseTranslation(@"This URL sends the subscription without TLS. Import it only if you trust this network and provider.", @"此 URL 未使用 TLS 传输订阅。请仅在信任网络和服务商时导入。");
    SenkoAddChineseTranslation(@"This build cannot dial this profile", @"此版本无法连接该配置");
    SenkoAddChineseTranslation(@"Tunnel routes", @"隧道路由");
    SenkoAddChineseTranslation(@"Unencrypted subscription", @"未加密的订阅");
    SenkoAddChineseTranslation(@"VARIANT", @"版本");
    SenkoAddChineseTranslation(@"Zero address", @"空地址");
    SenkoAddChineseTranslation(@"all", @"全部");
    SenkoAddChineseTranslation(@"checking", @"检查中");
    SenkoAddChineseTranslation(@"copied", @"已复制");
    SenkoAddChineseTranslation(@"hits", @"命中次数");
    SenkoAddChineseTranslation(@"loading", @"加载中");
    SenkoAddChineseTranslation(@"name and url required", @"请填写名称和 URL");
    SenkoAddChineseTranslation(@"no answer", @"无响应");
    SenkoAddChineseTranslation(@"not available yet", @"暂时不可用");
    SenkoAddChineseTranslation(@"nothing recorded", @"没有记录");
    SenkoAddChineseTranslation(@"passed in %d ms", @"耗时 %d 毫秒，通过");
    SenkoAddChineseTranslation(@"the daemon did not answer", @"守护进程没有响应");
    SenkoAddChineseTranslation(@"unavailable", @"不可用");
    SenkoAddChineseTranslation(@"utun tunnel", @"utun 隧道");
    SenkoAddChineseTranslation(@"(no log)", @"（无日志）");
    SenkoAddChineseTranslation(@"A live profile cannot be edited", @"正在使用的配置无法编辑");
    SenkoAddChineseTranslation(@"Accent", @"强调色");
    SenkoAddChineseTranslation(@"Accent low", @"强调色底部");
    SenkoAddChineseTranslation(@"Allow insecure", @"允许不安全连接");
    SenkoAddChineseTranslation(@"Almost done", @"即将完成");
    SenkoAddChineseTranslation(@"Amnezia VPN bundle detected. Export a native AmneziaWG .conf from Share", @"检测到 Amnezia VPN 配置包。请从“分享”导出原生 AmneziaWG .conf");
    SenkoAddChineseTranslation(@"Amnezia VPN bundle detected. Import a native AmneziaWG .conf file", @"检测到 Amnezia VPN 配置包。请导入原生 AmneziaWG .conf 文件");
    SenkoAddChineseTranslation(@"AmneziaWG", @"AmneziaWG");
    SenkoAddChineseTranslation(@"AmneziaWG DNS", @"AmneziaWG DNS");
    SenkoAddChineseTranslation(@"AmneziaWG link", @"AmneziaWG 链接");
    SenkoAddChineseTranslation(@"AmneziaWG route", @"AmneziaWG 路由");
    SenkoAddChineseTranslation(@"Apply anyway", @"仍然应用");
    SenkoAddChineseTranslation(@"Backend forced to", @"强制后端");
    SenkoAddChineseTranslation(@"Backend in use", @"当前后端");
    SenkoAddChineseTranslation(@"Backend pinned to", @"固定后端");
    SenkoAddChineseTranslation(@"Background", @"背景");
    SenkoAddChineseTranslation(@"Background low", @"背景底部");
    SenkoAddChineseTranslation(@"Battery", @"电池");
    SenkoAddChineseTranslation(@"Block response", @"阻止响应");
    SenkoAddChineseTranslation(@"Busiest rule", @"命中最多的规则");
    SenkoAddChineseTranslation(@"Bypass evicted", @"已移除的绕过项");
    SenkoAddChineseTranslation(@"Bypass evictions", @"绕过项移除次数");
    SenkoAddChineseTranslation(@"Bypass table", @"绕过表");
    SenkoAddChineseTranslation(@"CHOSEN", @"已选择");
    SenkoAddChineseTranslation(@"CONNECTION", @"连接");
    SenkoAddChineseTranslation(@"Camera access could not be requested", @"无法请求相机权限");
    SenkoAddChineseTranslation(@"Camera access is disabled\nEnable it in Settings > Privacy > Camera", @"相机权限已关闭\n请在设置 > 隐私 > 相机中开启");
    SenkoAddChineseTranslation(@"Catalog", @"服务器目录");
    SenkoAddChineseTranslation(@"Cell", @"列表项");
    SenkoAddChineseTranslation(@"Cell low", @"列表项底部");
    SenkoAddChineseTranslation(@"Check", @"检查");
    SenkoAddChineseTranslation(@"Checking TCP", @"正在检查 TCP");
    SenkoAddChineseTranslation(@"Checking package", @"正在检查软件包");
    SenkoAddChineseTranslation(@"Chrome", @"导航栏");
    SenkoAddChineseTranslation(@"Chrome low", @"导航栏底部");
    SenkoAddChineseTranslation(@"Config file", @"配置文件");
    SenkoAddChineseTranslation(@"Connected for", @"连接时长");
    SenkoAddChineseTranslation(@"Could not connect to the server. Check the address, network, and server availability.", @"无法连接服务器。请检查地址、网络和服务器状态。");
    SenkoAddChineseTranslation(@"Could not read the file", @"无法读取文件");
    SenkoAddChineseTranslation(@"Could not serialize theme", @"无法编码主题");
    SenkoAddChineseTranslation(@"Could not write to Documents", @"无法写入 Documents");
    SenkoAddChineseTranslation(@"Custom theme limit reached", @"自定义主题数量已达上限");
    SenkoAddChineseTranslation(@"DAEMON", @"守护进程");
    SenkoAddChineseTranslation(@"DEVICE", @"设备");
    SenkoAddChineseTranslation(@"DNS cache", @"DNS 缓存");
    SenkoAddChineseTranslation(@"DNS port", @"DNS 端口");
    SenkoAddChineseTranslation(@"DNS queries", @"DNS 查询");
    SenkoAddChineseTranslation(@"DNS settings apply the next time the tunnel comes up. The SOCKS port applies when the daemon restarts.", @"DNS 设置将在下次建立隧道时生效。SOCKS 端口将在守护进程重启后生效。");
    SenkoAddChineseTranslation(@"Daemon pid", @"守护进程 PID");
    SenkoAddChineseTranslation(@"Daemon uptime", @"守护进程运行时间");
    SenkoAddChineseTranslation(@"Device", @"设备");
    SenkoAddChineseTranslation(@"Device gating", @"设备限制");
    SenkoAddChineseTranslation(@"Device id file", @"设备 ID 文件");
    SenkoAddChineseTranslation(@"Device uptime", @"设备运行时间");
    SenkoAddChineseTranslation(@"Direct addresses", @"直连地址");
    SenkoAddChineseTranslation(@"Disconnect first", @"请先断开连接");
    SenkoAddChineseTranslation(@"Dropped packets", @"丢弃的数据包");
    SenkoAddChineseTranslation(@"Edit selected server", @"编辑选中的服务器");
    SenkoAddChineseTranslation(@"Egress address", @"出口地址");
    SenkoAddChineseTranslation(@"Egress interface", @"出口接口");
    SenkoAddChineseTranslation(@"Encrypted subscription", @"加密订阅");
    SenkoAddChineseTranslation(@"Every server in this content is already saved.", @"此内容中的所有服务器均已保存。");
    SenkoAddChineseTranslation(@"Export configuration", @"导出配置");
    SenkoAddChineseTranslation(@"FORCED", @"已强制");
    SenkoAddChineseTranslation(@"Felt", @"列表背景");
    SenkoAddChineseTranslation(@"Finishing", @"正在完成");
    SenkoAddChineseTranslation(@"Five taps on this heading hide the section again.", @"点击此标题五次即可再次隐藏该区域。");
    SenkoAddChineseTranslation(@"Flow", @"流控");
    SenkoAddChineseTranslation(@"Flush the DNS cache", @"清空 DNS 缓存");
    SenkoAddChineseTranslation(@"Free memory", @"可用内存");
    SenkoAddChineseTranslation(@"Gbit/s", @"Gbit/s");
    SenkoAddChineseTranslation(@"Jailbreak", @"越狱");
    SenkoAddChineseTranslation(@"Jailbreak root", @"越狱根目录");
    SenkoAddChineseTranslation(@"LIVE", @"实时");
    SenkoAddChineseTranslation(@"Last TCP error", @"上次 TCP 错误");
    SenkoAddChineseTranslation(@"Last UDP error", @"上次 UDP 错误");
    SenkoAddChineseTranslation(@"Last backend error", @"上次后端错误");
    SenkoAddChineseTranslation(@"Link", @"链接");
    SenkoAddChineseTranslation(@"Live connections", @"当前连接数");
    SenkoAddChineseTranslation(@"Manually added profiles only", @"仅手动添加的配置");
    SenkoAddChineseTranslation(@"No camera available", @"没有可用的相机");
    SenkoAddChineseTranslation(@"No configuration is selected. Pick a server first.", @"尚未选择配置。请先选择服务器。");
    SenkoAddChineseTranslation(@"No server Senko can run was found in this content.", @"此内容中没有 Senko 可用的服务器。");
    SenkoAddChineseTranslation(@"Not a Senko theme file", @"不是 Senko 主题文件");
    SenkoAddChineseTranslation(@"OFF", @"关闭");
    SenkoAddChineseTranslation(@"ON", @"开启");
    SenkoAddChineseTranslation(@"Offline", @"离线");
    SenkoAddChineseTranslation(@"Offline low", @"离线底部");
    SenkoAddChineseTranslation(@"Online", @"在线");
    SenkoAddChineseTranslation(@"Online low", @"在线底部");
    SenkoAddChineseTranslation(@"PATHS", @"路径");
    SenkoAddChineseTranslation(@"PROCESSES", @"进程");
    SenkoAddChineseTranslation(@"Packets", @"数据包");
    SenkoAddChineseTranslation(@"Ping All", @"检查全部延迟");
    SenkoAddChineseTranslation(@"Preparing package", @"正在准备软件包");
    SenkoAddChineseTranslation(@"QR code is empty or unreadable", @"二维码为空或无法读取");
    SenkoAddChineseTranslation(@"QR code not detected\nfill the frame with the code\nand hold the phone still", @"未检测到二维码\n请将二维码放入画面中央\n并保持设备稳定");
    SenkoAddChineseTranslation(@"Redial", @"重新拨号");
    SenkoAddChineseTranslation(@"Redirect port", @"重定向端口");
    SenkoAddChineseTranslation(@"Refresh the subscription to change it", @"刷新订阅以更新此值");
    SenkoAddChineseTranslation(@"Request header", @"请求头");
    SenkoAddChineseTranslation(@"Rule 2", @"规则 2");
    SenkoAddChineseTranslation(@"Rule 3", @"规则 3");
    SenkoAddChineseTranslation(@"Rule hits", @"规则命中次数");
    SenkoAddChineseTranslation(@"Rule verdicts", @"规则结果");
    SenkoAddChineseTranslation(@"Rules", @"规则");
    SenkoAddChineseTranslation(@"Running dpkg --install", @"正在运行 dpkg --install");
    SenkoAddChineseTranslation(@"Russian/English", @"俄语/英语");
    SenkoAddChineseTranslation(@"SOCKS bound to", @"SOCKS 绑定地址");
    SenkoAddChineseTranslation(@"SOCKS is localhost-only by default. socks_public=1 in config opens it to the LAN.", @"SOCKS 默认仅本机可用。在配置中设置 socks_public=1 可供局域网访问。");
    SenkoAddChineseTranslation(@"Scan native config", @"扫描原生配置");
    SenkoAddChineseTranslation(@"Second busiest rule", @"命中第二多的规则");
    SenkoAddChineseTranslation(@"See /tmp/senko-update.log", @"查看 /tmp/senko-update.log");
    SenkoAddChineseTranslation(@"Selected server", @"选中的服务器");
    SenkoAddChineseTranslation(@"Senko does not support this protocol", @"Senko 不支持此协议");
    SenkoAddChineseTranslation(@"Senko resident", @"Senko 常驻内存");
    SenkoAddChineseTranslation(@"Senko-core eligible", @"可使用 Senko-core");
    SenkoAddChineseTranslation(@"Senko-core usable", @"Senko-core 可用");
    SenkoAddChineseTranslation(@"Shadowsocks", @"Shadowsocks");
    SenkoAddChineseTranslation(@"Source", @"来源");
    SenkoAddChineseTranslation(@"Starting install helper", @"正在启动安装助手");
    SenkoAddChineseTranslation(@"Status bar hook", @"状态栏钩子");
    SenkoAddChineseTranslation(@"Stopping senkod", @"正在停止 senkod");
    SenkoAddChineseTranslation(@"Subscription profile", @"订阅配置");
    SenkoAddChineseTranslation(@"Substrate directory", @"Substrate 目录");
    SenkoAddChineseTranslation(@"System log", @"系统日志");
    SenkoAddChineseTranslation(@"TCP flows", @"TCP 连接流");
    SenkoAddChineseTranslation(@"TCP traffic", @"TCP 流量");
    SenkoAddChineseTranslation(@"TLS compatibility hook", @"TLS 兼容钩子");
    SenkoAddChineseTranslation(@"Tap Close when you are ready.", @"准备好后点击“关闭”。");
    SenkoAddChineseTranslation(@"Text", @"文字");
    SenkoAddChineseTranslation(@"Text muted", @"次要文字");
    SenkoAddChineseTranslation(@"The Happ crypt5 bundle on this page could not be opened. It is either damaged or sealed with a key this build does not carry.", @"无法打开此页面的 Happ crypt5 配置包。文件可能损坏，或使用了当前版本没有的密钥。");
    SenkoAddChineseTranslation(@"The Senko service is not responding. Restart it and try again.", @"Senko 服务没有响应。请重启后重试。");
    SenkoAddChineseTranslation(@"The clipboard is empty.", @"剪贴板为空。");
    SenkoAddChineseTranslation(@"The connect hook could not start. Check that senkotlsfix is installed, or pick another backend.", @"连接钩子无法启动。请检查是否已安装 senkotlsfix，或选择其他后端。");
    SenkoAddChineseTranslation(@"The connection attempt timed out. Check the network and try another server.", @"连接超时。请检查网络并尝试其他服务器。");
    SenkoAddChineseTranslation(@"The daemon runs these on its own, with the app closed.", @"即使应用关闭，守护进程也会自动执行这些操作。");
    SenkoAddChineseTranslation(@"The file is empty or could not be read.", @"文件为空或无法读取。");
    SenkoAddChineseTranslation(@"The local proxy could not start. Restart Senko and check that another copy is not running.", @"本地代理无法启动。请重启 Senko，并检查是否已有另一个实例正在运行。");
    SenkoAddChineseTranslation(@"The server address is invalid, unsafe, or cannot be resolved.", @"服务器地址无效、不安全或无法解析。");
    SenkoAddChineseTranslation(@"The server link has an invalid UUID. Import the link again from its source.", @"服务器链接中的 UUID 无效。请从来源重新导入链接。");
    SenkoAddChineseTranslation(@"The server name could not be resolved. Check the internet connection and server address.", @"无法解析服务器域名。请检查网络连接和服务器地址。");
    SenkoAddChineseTranslation(@"The server port is reachable, but the profile could not complete a real connection. Check its UUID or password, security, SNI, and transport settings.", @"服务器端口可访问，但配置无法完成实际连接。请检查 UUID 或密码、安全设置、SNI 和传输设置。");
    SenkoAddChineseTranslation(@"The tunnel could not be opened. Check the server settings, key, and selected transport.", @"无法打开隧道。请检查服务器设置、密钥和传输方式。");
    SenkoAddChineseTranslation(@"The tunnel could not start. Open System Logs to see whether utun, routes, or the bundled core failed.", @"隧道无法启动。请打开系统日志，查看 utun、路由或内置核心的错误。");
    SenkoAddChineseTranslation(@"The tunnel could not start. Open System Logs to see whether utun, routes, or the server failed.", @"隧道无法启动。请打开系统日志，查看 utun、路由或服务器的错误。");
    SenkoAddChineseTranslation(@"The tunnel stopped right after it started. Open System Logs for the reason.", @"隧道启动后立即停止。请在系统日志中查看原因。");
    SenkoAddChineseTranslation(@"Theme file is incomplete", @"主题文件不完整");
    SenkoAddChineseTranslation(@"Theme is not a custom theme", @"这不是自定义主题");
    SenkoAddChineseTranslation(@"There was nothing to import.", @"没有可导入的内容。");
    SenkoAddChineseTranslation(@"Third busiest rule", @"命中第三多的规则");
    SenkoAddChineseTranslation(@"This address opens a web page instead of a subscription feed. Copy the subscription link the page offers, not the page address.", @"此地址打开的是网页，而非订阅内容。请复制网页提供的订阅链接，不要复制网页地址。");
    SenkoAddChineseTranslation(@"This device received a link back to the same address instead of a subscription feed. Check device access and ask the provider for the feed URL.", @"此设备收到指向同一地址的链接，而不是订阅内容。请检查设备权限，并向服务商索取订阅地址。");
    SenkoAddChineseTranslation(@"This profile cannot be checked while another profile is connected. Disconnect first.", @"连接其他配置时无法检查此配置。请先断开连接。");
    SenkoAddChineseTranslation(@"This server uses a protocol or security mode that Senko does not support.", @"此服务器使用 Senko 不支持的协议或安全模式。");
    SenkoAddChineseTranslation(@"This theme will lag on iOS 6/7. Liquid glass is laggy on older device.", @"此主题在 iOS 6/7 上可能运行缓慢。旧设备上的玻璃效果性能较低。");
    SenkoAddChineseTranslation(@"Top rule", @"命中最多的规则");
    SenkoAddChineseTranslation(@"Trojan", @"Trojan");
    SenkoAddChineseTranslation(@"Tunnel DNS", @"隧道 DNS");
    SenkoAddChineseTranslation(@"Tunnel counters", @"隧道计数器");
    SenkoAddChineseTranslation(@"Tunnel state", @"隧道状态");
    SenkoAddChineseTranslation(@"UDP datagrams", @"UDP 数据报");
    SenkoAddChineseTranslation(@"UTILITIES", @"工具");
    SenkoAddChineseTranslation(@"Unknown content type. This is not a server link, a subscription, or a profile Senko can read.", @"未知内容类型。这不是 Senko 可读取的服务器链接、订阅或配置。");
    SenkoAddChineseTranslation(@"Unsupported theme format", @"不支持的主题格式");
    SenkoAddChineseTranslation(@"Upstream DNS", @"上游 DNS");
    SenkoAddChineseTranslation(@"Vision", @"Vision");
    SenkoAddChineseTranslation(@"WAIT", @"请稍候");
    SenkoAddChineseTranslation(@"Well", @"凹槽");
    SenkoAddChineseTranslation(@"What was chosen", @"当前选择");
    SenkoAddChineseTranslation(@"Write bundle", @"写入诊断包");
    SenkoAddChineseTranslation(@"amneziawg config not found", @"未找到 AmneziaWG 配置");
    SenkoAddChineseTranslation(@"amneziawg profile loaded", @"AmneziaWG 配置已加载");
    SenkoAddChineseTranslation(@"amneziawg profile removed", @"AmneziaWG 配置已移除");
    SenkoAddChineseTranslation(@"amneziawg profile saved", @"AmneziaWG 配置已保存");
    SenkoAddChineseTranslation(@"amneziawg timeout", @"AmneziaWG 超时");
    SenkoAddChineseTranslation(@"bars bottom", @"导航栏底部");
    SenkoAddChineseTranslation(@"bars top", @"导航栏顶部");
    SenkoAddChineseTranslation(@"bit/s", @"bit/s");
    SenkoAddChineseTranslation(@"cannot open folder", @"无法打开文件夹");
    SenkoAddChineseTranslation(@"checking amneziawg...", @"正在检查 AmneziaWG…");
    SenkoAddChineseTranslation(@"checking daemon...", @"正在检查守护进程…");
    SenkoAddChineseTranslation(@"checking group ping...", @"正在检查分组延迟…");
    SenkoAddChineseTranslation(@"checking ping...", @"正在检查延迟…");
    SenkoAddChineseTranslation(@"checking server...", @"正在检查服务器…");
    SenkoAddChineseTranslation(@"choose a .deb package", @"请选择 .deb 软件包");
    SenkoAddChineseTranslation(@"connected", @"已连接");
    SenkoAddChineseTranslation(@"connected button bottom", @"连接按钮底部");
    SenkoAddChineseTranslation(@"connected button top", @"连接按钮顶部");
    SenkoAddChineseTranslation(@"connecting", @"连接中");
    SenkoAddChineseTranslation(@"connecting...", @"连接中…");
    SenkoAddChineseTranslation(@"connection failed", @"连接失败");
    SenkoAddChineseTranslation(@"could not read amneziawg config", @"无法读取 AmneziaWG 配置");
    SenkoAddChineseTranslation(@"could not save amneziawg config", @"无法保存 AmneziaWG 配置");
    SenkoAddChineseTranslation(@"could not save native AmneziaWG config", @"无法保存原生 AmneziaWG 配置");
    SenkoAddChineseTranslation(@"could not start amneziawg", @"无法启动 AmneziaWG");
    SenkoAddChineseTranslation(@"could not stop amneziawg", @"无法停止 AmneziaWG");
    SenkoAddChineseTranslation(@"could not stop senkod", @"无法停止 senkod");
    SenkoAddChineseTranslation(@"daemon offline: cannot add", @"守护进程离线：无法添加");
    SenkoAddChineseTranslation(@"daemon offline: cannot edit", @"守护进程离线：无法编辑");
    SenkoAddChineseTranslation(@"daemon offline: cannot import", @"守护进程离线：无法导入");
    SenkoAddChineseTranslation(@"daemon offline: cannot save header", @"守护进程离线：无法保存请求头");
    SenkoAddChineseTranslation(@"disconnect to edit", @"请断开连接后编辑");
    SenkoAddChineseTranslation(@"disconnect to remove", @"请断开连接后移除");
    SenkoAddChineseTranslation(@"disconnect to reorder", @"请断开连接后调整顺序");
    SenkoAddChineseTranslation(@"disconnect to switch", @"请断开连接后切换");
    SenkoAddChineseTranslation(@"disconnect to switch backend", @"请断开连接后切换后端");
    SenkoAddChineseTranslation(@"disconnected button bottom", @"断开按钮底部");
    SenkoAddChineseTranslation(@"disconnected button top", @"断开按钮顶部");
    SenkoAddChineseTranslation(@"empty folder", @"文件夹为空");
    SenkoAddChineseTranslation(@"failed", @"失败");
    SenkoAddChineseTranslation(@"fetch failed: daemon offline", @"获取失败：守护进程离线");
    SenkoAddChineseTranslation(@"fetching subscription...", @"正在获取订阅…");
    SenkoAddChineseTranslation(@"file import failed", @"文件导入失败");
    SenkoAddChineseTranslation(@"folder", @"文件夹");
    SenkoAddChineseTranslation(@"group ping complete", @"分组延迟检查完成");
    SenkoAddChineseTranslation(@"group profile check complete", @"分组配置检查完成");
    SenkoAddChineseTranslation(@"iOS major", @"iOS 主版本");
    SenkoAddChineseTranslation(@"iOS read from", @"iOS 版本来源");
    SenkoAddChineseTranslation(@"iOS version read from", @"iOS 版本读取来源");
    SenkoAddChineseTranslation(@"idle", @"空闲");
    SenkoAddChineseTranslation(@"install a .deb", @"安装 .deb 软件包");
    SenkoAddChineseTranslation(@"install this package over the current version? settings and subscriptions stay in place", @"用此软件包覆盖当前版本？设置和订阅将保留");
    SenkoAddChineseTranslation(@"invalid amneziawg config", @"AmneziaWG 配置无效");
    SenkoAddChineseTranslation(@"invalid native AmneziaWG config", @"原生 AmneziaWG 配置无效");
    SenkoAddChineseTranslation(@"links and glyphs", @"链接和图标");
    SenkoAddChineseTranslation(@"list backdrop", @"列表背景");
    SenkoAddChineseTranslation(@"list inset tint", @"列表内侧色调");
    SenkoAddChineseTranslation(@"list reloaded", @"列表已刷新");
    SenkoAddChineseTranslation(@"manual", @"手动");
    SenkoAddChineseTranslation(@"manual profiles only", @"仅手动配置");
    SenkoAddChineseTranslation(@"manual servers removed", @"手动服务器已移除");
    SenkoAddChineseTranslation(@"native AmneziaWG config added", @"已添加原生 AmneziaWG 配置");
    SenkoAddChineseTranslation(@"no readable folders", @"没有可读取的文件夹");
    SenkoAddChineseTranslation(@"no servers in group", @"分组中没有服务器");
    SenkoAddChineseTranslation(@"none", @"无");
    SenkoAddChineseTranslation(@"paste a link here", @"在此粘贴链接");
    SenkoAddChineseTranslation(@"paste a subscription URL", @"粘贴订阅 URL");
    SenkoAddChineseTranslation(@"pick a server first", @"请先选择服务器");
    SenkoAddChineseTranslation(@"ping check complete", @"延迟检查完成");
    SenkoAddChineseTranslation(@"point the camera at a QR code\nserver link, subscription URL\nor a WireGuard / AmneziaWG .conf", @"将相机对准二维码\n服务器链接、订阅 URL\n或 WireGuard / AmneziaWG .conf");
    SenkoAddChineseTranslation(@"pressed accent", @"按下时的强调色");
    SenkoAddChineseTranslation(@"primary label", @"主要文字");
    SenkoAddChineseTranslation(@"profile check complete", @"配置检查完成");
    SenkoAddChineseTranslation(@"reading content...", @"正在读取内容…");
    SenkoAddChineseTranslation(@"refreshing subscription...", @"正在刷新订阅…");
    SenkoAddChineseTranslation(@"refreshing subscriptions...", @"正在刷新订阅…");
    SenkoAddChineseTranslation(@"removing manual servers...", @"正在移除手动服务器…");
    SenkoAddChineseTranslation(@"removing subscription...", @"正在移除订阅…");
    SenkoAddChineseTranslation(@"row bottom", @"列表项底部");
    SenkoAddChineseTranslation(@"row top", @"列表项顶部");
    SenkoAddChineseTranslation(@"saving subscription...", @"正在保存订阅…");
    SenkoAddChineseTranslation(@"secondary label", @"次要文字");
    SenkoAddChineseTranslation(@"section moved", @"分组已移动");
    SenkoAddChineseTranslation(@"senkod is missing: reinstall the package", @"缺少 senkod：请重新安装软件包");
    SenkoAddChineseTranslation(@"senkod is not running and its log is empty", @"senkod 未运行，日志为空");
    SenkoAddChineseTranslation(@"server moved", @"服务器已移动");
    SenkoAddChineseTranslation(@"server ping", @"服务器延迟");
    SenkoAddChineseTranslation(@"server ping timeout", @"服务器延迟检查超时");
    SenkoAddChineseTranslation(@"starting amneziawg...", @"正在启动 AmneziaWG…");
    SenkoAddChineseTranslation(@"subscription added", @"订阅已添加");
    SenkoAddChineseTranslation(@"subscription not found", @"未找到订阅");
    SenkoAddChineseTranslation(@"subscription pinned", @"订阅已置顶");
    SenkoAddChineseTranslation(@"subscription removed", @"订阅已移除");
    SenkoAddChineseTranslation(@"subscription saved", @"订阅已保存");
    SenkoAddChineseTranslation(@"subscription updated", @"订阅已更新");
    SenkoAddChineseTranslation(@"subscription url has spaces", @"订阅 URL 包含空格");
    SenkoAddChineseTranslation(@"subscriptions refreshed", @"订阅已刷新");
    SenkoAddChineseTranslation(@"switch timeout", @"切换超时");
    SenkoAddChineseTranslation(@"the senkod launch daemon is missing: reinstall the package", @"缺少 senkod 启动服务：请重新安装软件包");
    SenkoAddChineseTranslation(@"timeout", @"超时");
    SenkoAddChineseTranslation(@"unknown check type", @"未知检查类型");
    SenkoAddChineseTranslation(@"unknown error", @"未知错误");
    SenkoAddChineseTranslation(@"validated import", @"已验证导入内容");
    SenkoAddChineseTranslation(@"validating native config...", @"正在验证原生配置…");
    SenkoAddChineseTranslation(@"wallpaper bottom", @"壁纸底部");
    SenkoAddChineseTranslation(@"wallpaper top", @"壁纸顶部");
    SenkoAddChineseTranslation(@"Block wins over direct, direct wins over the tunnel, whatever the order. On iOS 12 and later the tunnel core reads the real domain from the connection; below that the rule is matched when the name is resolved, so an address shared by several sites follows the first name that asked for it.", @"规则顺序不会改变优先级：阻止优先于直连，直连优先于隧道。iOS 12 及以上版本会从连接中读取实际域名；旧版系统在解析域名时匹配规则，因此多个网站共用的地址会采用首次请求该地址的域名规则。");
    SenkoAddChineseTranslation(@"Senko failed to start %d times and is running with the stock theme. The report is in Logs.", @"Senko 连续 %d 次启动失败，当前使用默认主题。报告位于日志页面。");
    SenkoAddChineseTranslation(@"the legacy theme for the legacy community", @"为怀旧社区打造的经典主题");
    SenkoAddChineseTranslation(@"flat and transparent", @"扁平而通透");
    SenkoAddChineseTranslation(@"modern theme", @"现代主题");
    SenkoAddChineseTranslation(@"liquid ass... nah, glass", @"流动玻璃风格");
    SenkoAddChineseTranslation(@"meeeeeow :3", @"喵喵喵 :3");
    SenkoAddChineseTranslation(@"hehehe mita hehehe miside", @"嘿嘿，米塔来了");
    SenkoAddChineseTranslation(@"futuristic maximalism of the past", @"复古未来的绚丽风格");
    SenkoAddChineseTranslation(@"made on this device", @"在此设备上创建");
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
    else if ([raw isEqualToString:@"routing: the connect hook could not start"])
        message = @"The connect hook could not start. Check that senkotlsfix is installed, or pick another backend.";
    else if ([raw isEqualToString:@"routing: the tunnel stopped before its check could run"])
        message = @"The tunnel stopped right after it started. Open System Logs for the reason.";
    else if ([raw isEqualToString:@"tunnel: senko-core could not start; open System Logs for the exact cause"])
        message = @"The tunnel could not start. Open System Logs to see whether utun, routes, or the bundled core failed.";
    else if ([raw isEqualToString:@"server: unsupported protocol or security"])
        message = @"This server uses a protocol or security mode that Senko does not support.";
    else if ([raw isEqualToString:@"server: bad uuid in server link"])
        message = @"The server link has an invalid UUID. Import the link again from its source.";
    else if ([raw hasPrefix:@"this address only hands back its own link"])
        message = @"This device received a link back to the same address instead of a subscription feed. Check device access and ask the provider for the feed URL.";
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
    else if ([raw hasPrefix:@"routing:"])
        message = @"The connect hook could not start. Check that senkotlsfix is installed, or pick another backend.";
    else if ([raw hasPrefix:@"error endpoint udp"])
        message = @"Could not connect to the server. Check the address, network, and server availability.";
    else if ([raw hasPrefix:@"error utun"] || [raw hasPrefix:@"error route"])
        message = @"The tunnel could not start. Open System Logs to see whether utun, routes, or the server failed.";
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

NSString *SenkoHoursText(int hours) {
    if (SenkoLanguageIsChinese()) return [NSString stringWithFormat:@"%d 小时", hours];
    if (SenkoLanguageIsRussian())
        return [NSString stringWithFormat:@"%d %@", hours,
                SenkoRussianPlural(hours, @"час", @"часа", @"часов")];
    return [NSString stringWithFormat:hours == 1 ? @"%d hour" : @"%d hours", hours];
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
/* the skip summary senkod appends to an import: "skipped 3: vmess 2, tuic 1".
   protocol names stay as they are; the reasons senko words itself are translated */
static NSString *SenkoSkipTailRu(NSString *tail) {
    static NSString * const kPairs[][2] = {
        { @"skipped ", @"пропущено " },
        { @" already saved", @" уже есть" },
        { @"malformed link", @"битая ссылка" },
        { @"tcp with an http header", @"tcp с http-заголовком" },
        { @"httpupgrade transport", @"транспорт httpupgrade" },
        { @"kcp transport", @"транспорт kcp" },
        { @"quic transport", @"транспорт quic" },
        { @"trojan over reality", @"trojan через reality" },
        { @"vless encryption", @"шифрование vless" },
        { @"invalid vless id", @"неверный id vless" },
        { @"shadowsocks plugin", @"плагин shadowsocks" },
        { @"shadowsocks userinfo that does not decode", @"нечитаемые данные shadowsocks" },
        { @"shadowsocks cipher that is not a name", @"нечитаемый шифр shadowsocks" },
        { @"happ link that does not open", @"нераскрываемая ссылка happ" },
        { @"malformed profile entry", @"битая запись профиля" },
        { @"hysteria v1", @"hysteria 1" },
        { @"unsupported trojan transport", @"trojan через неподдерживаемый транспорт" },
        { @"shadowsocks over another transport", @"shadowsocks не через tcp" },
        { @"socks over tls", @"socks через tls" },
        { @"link too long", @"слишком длинная ссылка" },
        { @", other ", @", прочее " },
    };
    NSString *out = tail;
    for (size_t i = 0; i < sizeof kPairs / sizeof kPairs[0]; ++i)
        out = [out stringByReplacingOccurrencesOfString:kPairs[i][0] withString:kPairs[i][1]];
    return out;
}

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
        tail = [tail stringByReplacingOccurrencesOfString:@" from the first 512 KB"
                                               withString:@" из первых 512 КБ"];
        tail = [tail stringByReplacingOccurrencesOfString:@" (list full)"
                                               withString:@" (список заполнен)"];
        tail = SenkoSkipTailRu(tail);
        return [NSString stringWithFormat:@"%@ %d %@%@", kRussian[i], count,
                SenkoRussianPlural(count, @"сервер", @"сервера", @"серверов"), tail];
    }
    return nil;
}

/* the reason senkod gives for a failed subscription fetch */
static NSString *SenkoFetchReasonRu(NSString *why) {
    NSString *host = nil;
    if ([why hasPrefix:@"cannot resolve or connect to "])
        return [@"не удалось найти или подключиться к " stringByAppendingString:
                [why substringFromIndex:29]];
    if ([why hasPrefix:@"tls or connection to "] && [why hasSuffix:@" failed"]) {
        host = [why substringWithRange:NSMakeRange(21, [why length] - 21 - 7)];
        return [NSString stringWithFormat:@"ошибка TLS или соединения с %@", host];
    }
    NSRange r = [why rangeOfString:@" answered HTTP "];
    if (r.location != NSNotFound)
        return [NSString stringWithFormat:@"%@ ответил HTTP %@",
                [why substringToIndex:r.location], [why substringFromIndex:NSMaxRange(r)]];
    if ([why hasSuffix:@" sent a response senko cannot read"])
        return [NSString stringWithFormat:@"%@ прислал ответ, который senko не может прочитать",
                [why substringToIndex:[why length] - 34]];
    if ([why hasPrefix:@"the subscription from "] && [why hasSuffix:@" is too large"]) {
        host = [why substringWithRange:NSMakeRange(22, [why length] - 22 - 13)];
        return [NSString stringWithFormat:@"подписка с %@ слишком большая", host];
    }
    if ([why hasSuffix:@" redirected too often or to an unusable address"])
        return [NSString stringWithFormat:@"%@ перенаправляет слишком много раз или на неподходящий адрес",
                [why substringToIndex:[why length] - 47]];
    if ([why isEqualToString:@"the subscription url is invalid"])
        return @"неверная ссылка на подписку";
    if ([why isEqualToString:@"no reason given"])
        return @"причина неизвестна";
    return why;
}

static NSString *SenkoLocalizedDynamic(NSString *text) {
    if (![text length]) return text;

    if ([text hasPrefix:@"fetch failed: "])
        return [@"не удалось загрузить подписку: " stringByAppendingString:
                SenkoFetchReasonRu([text substringFromIndex:14])];
    if ([text hasPrefix:@"subscription added, refresh failed: "])
        return [@"подписка добавлена, но не загрузилась: " stringByAppendingString:
                SenkoFetchReasonRu([text substringFromIndex:36])];

    if ([text hasPrefix:@"subscription added, no server senko can run in the first 512 KB ("])
        return [@"подписка добавлена, но в первых 512 КБ нет серверов, которые senko может запустить ("
                stringByAppendingString:SenkoSkipTailRu([text substringFromIndex:65])];
    if ([text hasPrefix:@"subscription added, no server senko can run ("])
        return [@"подписка добавлена, но в ней нет серверов, которые senko может запустить ("
                stringByAppendingString:SenkoSkipTailRu([text substringFromIndex:45])];
    if ([text hasPrefix:@"subscription added, "] && [text rangeOfString:@" server"].location != NSNotFound) {
        NSString *count = SenkoServerCountReply([@"imported " stringByAppendingString:
                                                 [text substringFromIndex:20]]);
        if (count)
            return [@"подписка добавлена, " stringByAppendingString:
                    [count stringByReplacingOccurrencesOfString:@"импортировано " withString:@""]];
    }
    if ([text hasPrefix:@"no server senko can run in the first 512 KB ("])
        return [@"в первых 512 КБ подписки нет серверов, которые senko может запустить (" stringByAppendingString:
                SenkoSkipTailRu([text substringFromIndex:45])];
    if ([text hasPrefix:@"no server senko can run ("])
        return [@"нет серверов, которые senko может запустить (" stringByAppendingString:
                SenkoSkipTailRu([text substringFromIndex:25])];
    if ([text hasPrefix:@"no server senko can run in this file ("])
        return [@"в этом файле нет серверов, которые senko может запустить (" stringByAppendingString:
                SenkoSkipTailRu([text substringFromIndex:38])];

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
    if ([text hasPrefix:@"senkod is not running, last log line: "])
        return [NSString stringWithFormat:@"senkod не запущен, последняя строка лога: %@",
                [text substringFromIndex:38]];
    if ([text hasPrefix:@"Version "])
        return [NSString stringWithFormat:@"Версия %@", [text substringFromIndex:8]];
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
