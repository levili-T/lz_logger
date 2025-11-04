import Flutter
import UIKit
import lz_logger

@main
@objc class AppDelegate: FlutterAppDelegate {
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    
    // 初始化 lz_logger
    LZLogger.sharedInstance().prepareLog("testlog", encryptKey: "xiaozhaozhaozhaozhaozhao")
    
    // 设置 Performance Test Method Channel
    let controller : FlutterViewController = window?.rootViewController as! FlutterViewController
    let performanceChannel = FlutterMethodChannel(name: "lz_logger_performance_test",
                                                   binaryMessenger: controller.binaryMessenger)
    
    performanceChannel.setMethodCallHandler({
      [weak self] (call: FlutterMethodCall, result: @escaping FlutterResult) -> Void in
      guard call.method == "runPerformanceTests" else {
        result(FlutterMethodNotImplemented)
        return
      }
      self?.runPerformanceTests(result: result)
    })
    
    GeneratedPluginRegistrant.register(with: self)
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
  
  private func runPerformanceTests(result: @escaping FlutterResult) {
    PerformanceTestRunner.runPerformanceTests { results in
      result(results)
    }
  }
}
