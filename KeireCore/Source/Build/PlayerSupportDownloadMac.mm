#include "KeireInternal/Build/PlayerSupportCatalog.h"

#if defined(__APPLE__)
#include "KeireInternal/FileSystem.h"

#import <Foundation/Foundation.h>

#include <chrono>
#include <stdexcept>
#include <thread>

@interface KeireHttpsRedirectDelegate : NSObject <NSURLSessionTaskDelegate>
@end

@implementation KeireHttpsRedirectDelegate
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler
{
    (void)session;
    (void)task;
    (void)response;
    completionHandler([request.URL.scheme.lowercaseString isEqualToString:@"https"] ? request : nil);
}
@end

namespace Keire::Detail
{
    void DownloadHttpsFileNative(const std::string_view url, const std::filesystem::path& destination,
                                 const std::uint64_t maximumBytes, const PlayerSupportInstallCallbacks& callbacks)
    {
        @autoreleasepool
        {
            const auto encodedUrl = std::string(url);
            NSURL* source = [NSURL URLWithString:[NSString stringWithUTF8String:encodedUrl.c_str()]];
            if (!source || ![source.scheme.lowercaseString isEqualToString:@"https"])
                throw std::runtime_error("Build Support download URL is not valid HTTPS.");
            auto* delegate = [[KeireHttpsRedirectDelegate alloc] init];
            NSURLSession* session =
                [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration
                                              delegate:delegate
                                         delegateQueue:nil];
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
            __block NSError* failure = nil;
            __block std::uint64_t downloadedBytes = 0;
            const auto destinationUtf8 = PathToUtf8(destination);
            NSURLSessionDownloadTask* task = [session
                downloadTaskWithURL:source
                  completionHandler:^(NSURL* location, NSURLResponse* response, NSError* error) {
                    if (error)
                    {
                        failure = error;
                    }
                    else if (![response.URL.scheme.lowercaseString isEqualToString:@"https"] ||
                             ![response isKindOfClass:[NSHTTPURLResponse class]] ||
                             [(NSHTTPURLResponse*)response statusCode] != 200)
                    {
                        failure = [NSError errorWithDomain:@"KeireBuildSupport" code:1 userInfo:nil];
                    }
                    else
                    {
                        NSNumber* size = nil;
                        [location getResourceValue:&size forKey:NSURLFileSizeKey error:&failure];
                        downloadedBytes = size.unsignedLongLongValue;
                        if (!failure && downloadedBytes <= maximumBytes)
                        {
                            NSURL* target =
                                [NSURL fileURLWithPath:[NSString stringWithUTF8String:destinationUtf8.c_str()]];
                            [[NSFileManager defaultManager] copyItemAtURL:location toURL:target error:&failure];
                        }
                    }
                    dispatch_semaphore_signal(semaphore);
                  }];
            [task resume];
            while (dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC)) != 0)
            {
                if (callbacks.Cancelled && callbacks.Cancelled())
                    [task cancel];
                if (callbacks.Progress)
                {
                    const auto expected = task.countOfBytesExpectedToReceive;
                    callbacks.Progress(expected > 0 ? 0.8F * static_cast<float>(task.countOfBytesReceived) /
                                                          static_cast<float>(expected)
                                                    : 0.0F,
                                       "Downloading Build Support");
                }
            }
            [session finishTasksAndInvalidate];
            [delegate release];
            if (failure || downloadedBytes > maximumBytes || !std::filesystem::is_regular_file(destination))
                throw std::runtime_error("Build Support HTTPS download failed.");
        }
    }
} // namespace Keire::Detail
#endif
