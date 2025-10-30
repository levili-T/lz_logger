#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface LZLogger : NSObject

+ (void)logWithLevel:(NSInteger)level
                tag:(NSString *)tag
               file:(NSString *)file
               line:(NSInteger)line
            message:(NSString *)message;

@end

NS_ASSUME_NONNULL_END
