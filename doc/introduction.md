### 提纲
- 代码整体结构
- 整体流程
- 线程梳理
- 解码过程
- 音画同步


---   
### 代码整体结构
- [src/main.cpp](https://github.com/sometao/littlePlayer/blob/master/src/main.cpp)：入口文件，解析用户输入的参数，主要是播放文件路径
- [src/playVideoWithAudio.cpp](https://github.com/sometao/littlePlayer/blob/master/src/playVideoWithAudio.cpp)
  - 通过`PacketGrabber`完成解封装，获得AVPacket
  - 通过`VideoProcessor`对视频AVPacket进行解码
  - 通过`AudioProcessor`对音频AVPacket进行解码
  - 调用SDL完成视频输出
  - 管理SDL完成音频输出
- src/pch.cpp：提高编译效率使用，跟代码逻辑无关，可以无视，具体原理可以百度
- [include/ffmpegUtil.h](https://github.com/sometao/littlePlayer/blob/master/include/ffmpegUtil.h)
  - `PacketGrabber`类：解封装工具类，方便的从文件或者媒体流中，读出AVPacket
  - 提供`ffUtils::initCodecContext`初始化编码器的工具方法
  - `ReSampler`类：音频重采用工具类，因为重采样流程有点麻烦，所以就写了这个工具类
  - `AudioInfo`类：将音频信息集中保存（采样率，声道，格式，布局）
- [include/MediaProcessor.h](https://github.com/sometao/littlePlayer/blob/master/include/MediaProcessor.hpp)
  - `MediaProcessor`是一个抽象类/纯虚类，向其喂入AVPacket，可以从中读取解码后的内容
  - `AudioProcessor`是`MediaProcessor`的子类，处理音频解码流程，并完成重采样，提供最终音频数据
  - `VideoProcessor`是`MediaProcessor`的子类，处理视频解码流程，并完成画面转换，提供最终AVFrame数据
- src/pch.h：提高编译效率使用，跟代码逻辑无关，可以无视，具体原理可以百度

### 整体流程
- [初始化](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/src/playVideoWithAudio.cpp#L290)
  - 对PacketGrabber进行初始化，初始化后PacketGrabber内部维护formatCtx
  - 通过formatCtx，来对VideoProcessor和AudioProcessor进行初始化
- 解封装
  - readerThread线程，线程函数`pktReader`，循环从`packetGrabber`中读取`AVPacket`传递给`VideoProcessor`和`VideoProcessor`
- 解码
  - 视频解码：VideoProcessor对喂进来的AVPacket进行解码，画面转换，将处理完毕的AVFrame保存下来，等待取用
  - 音频解码：AudioProcessor对喂进来的AVPacket进行解码，音频重采用，将处理完毕的音频数据保存下来，等待取用
- 播放
  - 音频播放：音频播放使用了SDL的音频播放流畅，SDL会通过回调函数间歇性从AudioProcessor拉取准备好的音频数据进行播放，我们要做的只是配置SDL音频参数，并启动SDL音频播放，这个配置以及启动的工作由`startAudioThread`线程通过`startSdlAudio`函数完成；
  - 视频播放：视频播放比音频播放复杂了一些，`videoThread`线程通过`playSdlVideo`函数维护视频播放的整个周期，首先初始化SDL的窗口，然后启动一个画面播放的计时线程，这个计时线程按照画面帧率发送播放指令，播放线程收到这个指令就会调用`VideoProcessor.getFrame()`获取画面绘制到屏幕上


### 主要业务线程梳理
- [readerThread](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/src/playVideoWithAudio.cpp#L35)：实现函数为`pktReader`，从PacketGrabber中读取`AVPacket`传递给`VideoProcessor`和`VideoProcessor`
- [startAudioThread](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/src/playVideoWithAudio.cpp#L199)：实现函数为`startSdlAudio`，对SDL的音频参数进行配置，并启动音频播放，因为SDL音频初始化时需要知道每帧（AVFrame）中有多少个音频sample，而这个值需要等到有AVFrame被解码出来之后才能知道，所以这个线程中就有个while等待这个数值被确定，这也是为什么启动流程需要搞一个线程来处理。
- [videoThread](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/src/playVideoWithAudio.cpp#L83)：实现函数为`playSdlVideo`，对SDL窗口进行初始化，并调用`VideoProcessor.getFrame()`获取画面绘制到屏幕上，至于什么时候获取画面，这个就由其子线程`refreshThread`控制；
- [refreshThread](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/src/playVideoWithAudio.cpp#L68)：实现函数为`picRefresher`，是由`videoThread`创建的子线程，按照帧率发送播放事件给`videoThread`，当画面播放速度落后音频太多时，就会加速播放；

### 解码流程
#### 视频解码流程：[VideoProcessor](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/include/MediaProcessor.hpp#L333)
1. VideoProcessor内部有一个`packetList`，用以存放解封装后的pkt，当packetList内的pkt少于PKT_WAITING_SIZE（设置为3）时，就会`needPacket()`就会返回true，外部就会通过`pushPkt()`向其喂入pkt；
- 在`start()`时，内部会创建一个[keeper线程](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/include/MediaProcessor.hpp#L33)，该线程会在`NextData`被消耗后，调用`prepareNextData()`来产生新的`NextData`
- 对于视频来说，`prepareNextData()`就是从解码器获得新得AVFrame，再对其进行画面转换，生成一个新的`outPic`，以备使用；
- 外部(`videoThread`)通过`getFrame()`取得`outPic`
-外部(`videoThread`)对`outPic`使用完毕后，通过调用`refreshFrame()`告知VideoProcessor `outPic`已失效
- `outPic`失效后，内部线程将去准备新的数据

#### 音频解码流程：[AudioProcessor](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/include/MediaProcessor.hpp#L205)
1. AudioProcessor这部分，与视频解码流程得第一点完全相同
- AudioProcessor这部分，与视频解码流程得第二点完全相同
- 对于音频来说，`prepareNextData()`就是从解码器获得新得AVFrame，再对其进行音频重采样，生成一个新的`outBuffer`，以备使用；
- 外部(SDL的音频回调函数)通过`writeAudioData()`来使用`outBuffer`中的数据；
- 当`outBuffer`被使用后，内部线程将去准备新的数据

#### [MediaProcessor](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/include/MediaProcessor.hpp#L22)
大家肯定都发现了，VideoProcessor和AudioProcessor的解码的五个环节中，1，2，5几乎完全相同，所以就可以把这部代码提出来，放到了他们的父类(基类)`MediaProcessor`中；
VideoProcessor和AudioProcessor各自只处理不同的部分
- 画面转换 vs 音频重采样
- 提供`getFrame()`返回`outPic`  vs 提供`writeAudioData()`来使用音频`outBuffer`
- 画面还得提供`refreshFrame()`方法来确认`outPic`使用完毕；

### 音画同步
- 音频：SDL会定期的调用`writeAudioData()`来获得音频数据进行播放，简单来说，音频播放我们不用控制，SDL来要数据我们就给数据
- 视频：在`videoThread`中会按照视频的帧率来给``videoThread``设置消息间隔，所以默认情况下，视频会按照帧率进行播放；
- 同步：`videoThread`线程在播放画面时，会[检查音频和视频当前播放的时间戳](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/src/playVideoWithAudio.cpp#L133)，当视频播放过快时，就会停一次视频刷新，当音频播放过快时，就会让``videoThread``二倍速发送播放事件，这样就达到了音视频的动态同步
- 时间戳：视频帧上获得的都是PTS，而音频和视频的PTS单位不同，需要[转化到时间戳（毫秒数）](https://github.com/sometao/littlePlayer/blob/9284f4c1fd0da3ac08c0dab0a30365ce1c821146/include/MediaProcessor.hpp#L339)上来进行比较判断，PTS和时间戳这些东西有点复杂，具体可以看看网上的资料


