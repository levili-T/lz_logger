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
    
    GeneratedPluginRegistrant.register(with: self)
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
}
