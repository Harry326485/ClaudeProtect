#import <Foundation/Foundation.h>
#include <locale.h>
#include <time.h>
int main(int argc, const char **argv) {
    @autoreleasepool {
        setlocale(LC_ALL, "");
        tzset();
        time_t dates[] = {1768478400, 1784116800};
        NSMutableArray *offsets = [NSMutableArray array];
        for (int i = 0; i < 2; i++) {
            struct tm local;
            localtime_r(&dates[i], &local);
            [offsets addObject:@{ @"epoch": @(dates[i]), @"libcOffset": @(local.tm_gmtoff),
                @"foundationOffset": @([[NSTimeZone localTimeZone] secondsFromGMTForDate:[NSDate dateWithTimeIntervalSince1970:dates[i]]]) }];
        }
        NSDictionary *report = @{
            @"libcLocale": @(setlocale(LC_ALL, NULL)),
            @"foundationLocale": [[NSLocale currentLocale] localeIdentifier],
            @"preferredLanguages": [NSLocale preferredLanguages],
            @"foundationTimeZone": [[NSTimeZone localTimeZone] name],
            @"offsets": offsets
        };
        NSData *data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
        puts([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding].UTF8String);
    }
    return 0;
}
