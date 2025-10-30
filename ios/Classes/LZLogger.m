#import "LZLogger.h"

#include "../../src/lz_logger.h"

@implementation LZLogger

+ (void)logWithLevel:(NSInteger)level
                tag:(NSString *)tag
               file:(NSString *)file
               line:(NSInteger)line
            message:(NSString *)message {
  lz_logger((int)level,
            tag.UTF8String,
            file.UTF8String,
            (int)line,
            message.UTF8String);
}

@end
