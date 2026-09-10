#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import "app_common.h"

NSString * const SenkoLanguageDidChangeNotification = @"SenkoLanguageDidChangeNotification";

static NSMutableDictionary *gEnglishToRussian;
static NSMutableDictionary *gRussianToEnglish;
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

static void SenkoBuildTranslations(void) {
    if (gEnglishToRussian) return;
    gEnglishToRussian = [[NSMutableDictionary alloc] init];
    gRussianToEnglish = [[NSMutableDictionary alloc] init];

    SenkoAddTranslation(@"About", @"О приложении");
    SenkoAddTranslation(@"iOS 5 cannot use the kernel firewall safely. Install MobileSubstrate and reinstall Senko so the application proxy can carry traffic.",
                        @"На iOS 5 нельзя безопасно использовать сетевой фильтр ядра. Установите MobileSubstrate и переустановите Senko, чтобы трафик пошёл через прокси приложений.");
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
    SenkoAddTranslation(@"Hide server links", @"Скрывать ссылки серверов");
    SenkoAddTranslation(@"Hide links", @"Скрывать ссылки");
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
    SenkoAddTranslation(@"Hide links only changes what is shown on screen. Backups keep the complete configuration.", @"Скрытие ссылок влияет только на экран. В резервной копии сохраняется полная конфигурация.");
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
    SenkoAddTranslation(@"Check type", @"Тип проверки");
    SenkoAddTranslation(@"TCP port only", @"Только доступность TCP-порта");
    SenkoAddTranslation(@"Through current local proxy", @"Через текущий локальный прокси");
    SenkoAddTranslation(@"Current tunnel internet access", @"Доступ в интернет через текущий туннель");
    SenkoAddTranslation(@"Full profile check", @"Полная проверка профиля");
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
    SenkoAddTranslation(@"VPN badge in status bar", @"Значок VPN в статус-баре");
    SenkoAddTranslation(@"Turn off if the wifi glyph disappears", @"Выключите, если пропадает значок wi-fi");
    SenkoAddTranslation(@"No app fault report", @"Отчётов о сбоях нет");
    SenkoAddTranslation(@"Loading logs...", @"Загрузка логов...");
    SenkoAddTranslation(@"reading content...", @"чтение содержимого...");
    SenkoAddTranslation(@"removing manual servers...", @"удаление серверов...");
    SenkoAddTranslation(@"manual servers removed", @"серверы удалены");
    SenkoAddTranslation(@"Add subscription", @"Добавить подписку");
    SenkoAddTranslation(@"Paste from clipboard", @"Вставить из буфера");
    SenkoAddTranslation(@"Paste", @"Вставить");
    SenkoAddTranslation(@"Safe mode", @"Безопасный режим");
    SenkoAddTranslation(@"Copy link", @"Копировать ссылку");
    SenkoAddTranslation(@"Copied", @"Скопировано");
    SenkoAddTranslation(@"Whole device, set up by the root daemon",
                        @"Всё устройство, настраивает root-демон");
    SenkoAddTranslation(@"Routing is not a switch. Senko always carries every app "
                         "and every system connection, because senkod runs as root on "
                         "the jailbreak and rewrites the system routes itself, so there "
                         "is nothing to configure outside this app. The local proxy is "
                         "reachable only from this device.",
                        @"Маршрутизация — не переключатель. Senko всегда ведёт весь "
                         "трафик приложений и системы, потому что senkod работает от "
                         "root на джейлбрейке и сам переписывает системные маршруты: "
                         "настраивать что-то вне приложения не нужно. Локальный прокси "
                         "доступен только с этого устройства.");
    SenkoAddTranslation(@"Senko failed to start %d times and is running with "
                         "the stock theme. The report is in Logs.",
                        @"Senko не смог запуститься %d раза и работает со "
                         "стандартной темой. Отчёт — в разделе «Логи».");
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
    SenkoAddTranslation(@"PORT", @"ПОРТ");
    SenkoAddTranslation(@"Preparing package", @"Подготовка пакета");
    SenkoAddTranslation(@"Refresh", @"Обновить");
    SenkoAddTranslation(@"Refresh now", @"Обновить сейчас");
    SenkoAddTranslation(@"Refresh the subscription to change it", @"Обновите подписку, чтобы изменить её");
    SenkoAddTranslation(@"Remove", @"Удалить");
    SenkoAddTranslation(@"Restarting senkod", @"Перезапуск senkod");
    SenkoAddTranslation(@"Routing", @"Маршрутизация");
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
    SenkoAddTranslation(@"checking group profiles...", @"проверка профилей группы...");
    SenkoAddTranslation(@"checking ping...", @"проверка пинга...");
    SenkoAddTranslation(@"checking server...", @"проверка сервера...");
    SenkoAddTranslation(@"server ping timeout", @"сервер не ответил вовремя");
    SenkoAddTranslation(@"server ping", @"пинг сервера");
    SenkoAddTranslation(@"failed", @"ошибка");
    SenkoAddTranslation(@"checking", @"проверка");
    SenkoAddTranslation(@"WAIT", @"ЖДИТЕ");
    SenkoAddTranslation(@"profile works", @"профиль работает");
    SenkoAddTranslation(@"profile check failed", @"профиль не прошёл проверку");
    SenkoAddTranslation(@"checking profiles...", @"проверка профилей...");
    SenkoAddTranslation(@"disconnect to check profiles", @"отключитесь, чтобы проверить профили");
    SenkoAddTranslation(@"cannot open folder", @"не удалось открыть папку");
    SenkoAddTranslation(@"empty folder", @"папка пуста");
    SenkoAddTranslation(@"hidden :3", @"скрррыто :3");
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
    SenkoAddTranslation(@"Editing", @"Правим");
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
    SenkoAddTranslation(@"Source", @"Источник");
    SenkoAddTranslation(@"Link", @"Ссылка");
    SenkoAddTranslation(@"loading", @"загрузка");
    SenkoAddTranslation(@"unavailable", @"недоступна");
    SenkoAddTranslation(@"copied", @"скопирована");
    SenkoAddTranslation(@"unreachable", @"нет ответа");
    SenkoAddTranslation(@"Ping", @"Пинг");
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
}

BOOL SenkoLanguageIsRussian(void) {
    return [[[NSUserDefaults standardUserDefaults] objectForKey:SENKO_LANGUAGE_KEY] boolValue];
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
    else if ([raw hasPrefix:@"routing: ios 5 "] || [raw hasPrefix:@"error ios 5 "])
        message = @"iOS 5 cannot use the kernel firewall safely. Install MobileSubstrate and reinstall Senko so the application proxy can carry traffic.";
    else if ([raw hasPrefix:@"routing:"])
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
    if (!SenkoLanguageIsRussian()) return english;
    SenkoBuildTranslations();
    NSString *russian = [gEnglishToRussian objectForKey:english];
    return russian ? russian : SenkoLocalizedDynamic(english);
}

void SenkoSetLanguage(BOOL russian) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setBool:russian forKey:SENKO_LANGUAGE_KEY];
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

- (id)senko_initWithBarButtonSystemItem:(UIBarButtonSystemItem)item
                                target:(id)target
                                action:(SEL)action {
    id result = [self senko_initWithBarButtonSystemItem:item target:target action:action];
    NSString *raw = nil;
    switch (item) {
        case UIBarButtonSystemItemDone: raw = @"Done"; break;
        case UIBarButtonSystemItemCancel: raw = @"Cancel"; break;
        case UIBarButtonSystemItemSave: raw = @"Save"; break;
        case UIBarButtonSystemItemRefresh: raw = @"Refresh"; break;
        default: break;
    }
    if (result && raw) {
        objc_setAssociatedObject(result, &kSenkoRawBarTitle, raw, OBJC_ASSOCIATION_COPY_NONATOMIC);
        [result senko_setTitle:SenkoLocalizedText(raw)];
    }
    return result;
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
