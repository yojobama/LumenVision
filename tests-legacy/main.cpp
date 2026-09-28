//#include <cstdio>
//#include <iostream>
//#include "../LumenCore/Manager.h"
//#include <unistd.h>
//
//using namespace std;
//
//int main()
//{
//    Manager* manager = new Manager();
//    
//    vector<CameraHardwareInfo> cameras = manager->enumerateAvailableCameras();
//
//    for (CameraHardwareInfo info : cameras) {
//        cout << info.name << endl;
//    }
//
//    int sink = manager->createApriltagSink();
//    int sink2 = manager->createApriltagSink();
//
//    int source = manager->createImageFileSource("/mnt/c/Users/yojob/Downloads/apriltag.png");
//    //int source = manager->createVideoFileSource("/mnt/c/Users/yojob/Downloads/apriltagVideo.mp4", 60);
//
//    manager->bindSourceToSink(source, sink);
//    manager->bindSourceToSink(source, sink2);
//
//    manager->startAllSources();
//    manager->startAllSinks();
//
//    sleep(1);
//    
//	manager->getAllSinkResults();
//    
//    sleep(1);
//
//    manager->stopAllSources();
//    manager->stopAllSinks();
//
//    cout << "sink 1: " << manager->getSinkResult(sink) << endl;
//    cout << "sink 2: " << manager->getSinkResult(sink2) << endl;
//
//    return 0;
//}

int main() 
{
	return 0;
}