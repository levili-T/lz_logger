//
//  PerformanceTestRunner.h
//  Runner
//
//  Performance test runner for LZLogger
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface PerformanceTestRunner : NSObject

+ (void)runPerformanceTestsWithCompletion:(void (^)(NSDictionary *results))completion;

@end

NS_ASSUME_NONNULL_END
