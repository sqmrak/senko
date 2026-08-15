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
    SenkoAddTranslation(@"Add", @"Добавить");
    SenkoAddTranslation(@"Add server", @"Добавить сервер");
    SenkoAddTranslation(@"Allow insecure", @"Разрешить небезопасное");
    SenkoAddTranslation(@"Almost done", @"Почти готово");
    SenkoAddTranslation(@"AmneziaWG", @"AmneziaWG");
    SenkoAddTranslation(@"Apply anyway", @"Всё равно применить");
    SenkoAddTranslation(@"Appearance", @"Оформление");
    SenkoAddTranslation(@"Cancel", @"Отмена");
    SenkoAddTranslation(@"Check ping", @"Проверить пинг");
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
    SenkoAddTranslation(@"Install failed", @"Ошибка установки");
    SenkoAddTranslation(@"Installing", @"Установка");
    SenkoAddTranslation(@"Light", @"Светлая");
    SenkoAddTranslation(@"Language", @"Язык");
    SenkoAddTranslation(@"Manual", @"Вручную");
    SenkoAddTranslation(@"NAME", @"НАЗВАНИЕ");
    SenkoAddTranslation(@"Name", @"Название");
    SenkoAddTranslation(@"No camera available", @"Камера недоступна");
    SenkoAddTranslation(@"No daemon logs available", @"Логи демона недоступны");
    SenkoAddTranslation(@"OK", @"ОК");
    SenkoAddTranslation(@"OFF", @"ВЫКЛ");
    SenkoAddTranslation(@"ON", @"ВКЛ");
    SenkoAddTranslation(@"Paste link", @"Вставить ссылку");
    SenkoAddTranslation(@"Ping All", @"Пинг");
    SenkoAddTranslation(@"PORT", @"ПОРТ");
    SenkoAddTranslation(@"Preparing package", @"Подготовка пакета");
    SenkoAddTranslation(@"Refresh", @"Обновить");
    SenkoAddTranslation(@"Refresh now", @"Обновить сейчас");
    SenkoAddTranslation(@"Refresh the subscription to change it", @"Обновите подписку, чтобы изменить её");
    SenkoAddTranslation(@"Remove", @"Удалить");
    SenkoAddTranslation(@"Restarting senkod", @"Перезапуск senkod");
    SenkoAddTranslation(@"Routing", @"Маршрутизация");
    SenkoAddTranslation(@"routing: ios 5 full-device routing is disabled for safety", @"маршрутизация: режим всего устройства на ios 5 отключён для безопасности");
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
    SenkoAddTranslation(@"Themes", @"Темы");
    SenkoAddTranslation(@"Type link", @"Ввести ссылку");
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
    SenkoAddTranslation(@"install a .deb", @"установить .deb");
    SenkoAddTranslation(@"install this package over the current version? settings and subscriptions stay in place", @"установить этот пакет поверх текущей версии? настройки и подписки сохранятся");
    SenkoAddTranslation(@"invalid amneziawg config", @"некорректная конфигурация amneziawg");
    SenkoAddTranslation(@"invalid native AmneziaWG config", @"некорректная нативная конфигурация AmneziaWG");
    SenkoAddTranslation(@"manual profiles only", @"только ручные профили");
    SenkoAddTranslation(@"manual", @"вручную");
    SenkoAddTranslation(@"name and url required", @"нужны название и URL");
    SenkoAddTranslation(@"native AmneziaWG config added", @"нативная конфигурация AmneziaWG добавлена");
    SenkoAddTranslation(@"no links in file", @"в файле нет ссылок");
    SenkoAddTranslation(@"no readable folders", @"нет доступных папок");
    SenkoAddTranslation(@"no servers in group", @"в группе нет серверов");
    SenkoAddTranslation(@"no servers to ping", @"нет серверов для проверки");
    SenkoAddTranslation(@"not a valid server or subscription link", @"это не ссылка на сервер или подписку");
    SenkoAddTranslation(@"paste a link here", @"вставьте ссылку");
    SenkoAddTranslation(@"paste a subscription URL", @"вставьте URL подписки");
    SenkoAddTranslation(@"pick a server first", @"сначала выберите сервер");
    SenkoAddTranslation(@"ping check complete", @"проверка пинга завершена");
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
    SenkoAddTranslation(@"Senko-Boykisser is Light only: pink paper and falling boykissers on the home screen.", @"Senko-Boykisser только светлая.");
    SenkoAddTranslation(@"Senko-Aero is Light only: sky wallpaper and floating gloss bubbles.", @"Senko-Aero только светлая: обои с небом и парящие глянцевые пузыри");
    SenkoAddTranslation(@"the legacy theme for the legacy community", @"для выживших на iOS 6 :D");
    SenkoAddTranslation(@"flat and transparent", @"эстетика 2013 года");
    SenkoAddTranslation(@"meeeeeow :3", @"самая фембойская тема");
    SenkoAddTranslation(@"hehehe mita hehehe miside", @"ыыыыы кепочка ыыыыы мисайд");
    SenkoAddTranslation(@"futuristic maximalism of the past", @"эстетика, опередившая свое время");
    SenkoAddTranslation(@"modern theme", @"dopamine, palera1n, trollstore и вайб 2021 года");
    SenkoAddTranslation(@"liquid ass... nah, glass", @"нууу такое... tahoe!");
    SenkoAddTranslation(@"QR code not detected\nuse native AmneziaWG / WireGuard .conf\nlegacy and 2.0 formats", @"QR-код не обнаружен\nиспользуйте нативный .conf AmneziaWG / WireGuard\nформаты legacy и 2.0");
    SenkoAddTranslation(@"native AmneziaWG / WireGuard .conf\nlegacy and 2.0 formats\nAmnezia VPN vpn:// bundles are not supported", @"нативный .conf AmneziaWG / WireGuard\nформаты legacy и 2.0\nпакеты Amnezia VPN vpn:// не поддерживаются");
    SenkoAddTranslation(@"Amnezia VPN bundle detected. Export a native AmneziaWG .conf from Share", @"Обнаружен пакет Amnezia VPN. Экспортируйте нативный файл AmneziaWG .conf через Share");
    SenkoAddTranslation(@"Amnezia VPN bundle detected. Import a native AmneziaWG .conf file", @"Обнаружен пакет Amnezia VPN. Импортируйте нативный файл AmneziaWG .conf");
    SenkoAddTranslation(@"A live profile cannot be edited", @"Активный профиль нельзя изменить");
    SenkoAddTranslation(@"SOCKS is localhost-only by default. socks_public=1 in config opens it to the LAN.", @"SOCKS по умолчанию доступен только локально. socks_public=1 в конфигурации открывает его для сети");
    SenkoAddTranslation(@"Starting install helper", @"Запуск установщика");
    SenkoAddTranslation(@"Tap Close when you are ready.", @"Нажмите «Закрыть», когда будете готовы");
    SenkoAddTranslation(@"unknown error", @"неизвестная ошибка");
    SenkoAddTranslation(@"See /tmp/senko-update.log", @"См. /tmp/senko-update.log");
    SenkoAddTranslation(@"(no log)", @"(нет лога)");
}

BOOL SenkoLanguageIsRussian(void) {
    return [[[NSUserDefaults standardUserDefaults] objectForKey:SENKO_LANGUAGE_KEY] boolValue];
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

static NSString *SenkoLocalizedDynamic(NSString *text) {
    if (![text length]) return text;

    NSRange r = [text rangeOfString:@" server"];
    if (r.location != NSNotFound) {
        NSString *numberText = [text substringToIndex:r.location];
        NSInteger number = [numberText integerValue];
        if ([numberText length] && number >= 0) {
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
        if ([numberText length] && number >= 0) {
            NSString *tail = [text substringFromIndex:r.location + r.length];
            if ([tail hasPrefix:@"s"]) tail = [tail substringFromIndex:1];
            return [NSString stringWithFormat:@"%ld %@%@", (long)number,
                    SenkoRussianPlural(number, @"отдельная конфигурация", @"отдельные конфигурации", @"отдельных конфигураций"), tail];
        }
    }
    if ([text hasPrefix:@"imported "]) {
        NSScanner *scanner = [NSScanner scannerWithString:text];
        [scanner scanString:@"imported " intoString:nil];
        int count = 0;
        if ([scanner scanInt:&count])
            return [NSString stringWithFormat:@"импортировано ссылок: %d", count];
    }
    if ([text hasPrefix:@"importing "]) {
        NSScanner *scanner = [NSScanner scannerWithString:text];
        [scanner scanString:@"importing " intoString:nil];
        int count = 0;
        if ([scanner scanInt:&count])
            return [NSString stringWithFormat:@"импорт ссылок: %d...", count];
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
        return [NSString stringWithFormat:@"не удалось запустить демон %@", [text substringFromIndex:21]];
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
