import Flutter
import UIKit
import lz_logger

@main
@objc class AppDelegate: FlutterAppDelegate {
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    // 初始化日志系统
    let success = LZLogger.shared().prepareLog("laozhaozhao", encryptKey: nil)
    if success {
      LZLogger.shared().log(.info, file: #file, function: #function, line: #line, 
                           tag: "AppDelegate", format: "Logger initialized successfully")
    } else {
      print("Failed to initialize logger")
    }
    
    GeneratedPluginRegistrant.register(with: self)
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
}
