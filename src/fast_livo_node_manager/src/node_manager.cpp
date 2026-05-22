#include <rclcpp/rclcpp.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>

//git尝试
class NodeManager : public rclcpp::Node
{
public:
  NodeManager()
  : Node("fast_livo_node_manager")
  {
    fifo_path_ = "/tmp/fast_livo_cmd";

    start_fast_livo_script_ = "/home/ash/scripts/start_fast_livo.sh";
    play_bag_script_ = "/home/ash/scripts/play_cbd.sh";

    ensure_fifo();

    running_ = true;

    //std::thread( 要执行的函数, 调用这个函数的对象 )     如果fifo_loop不是成员函数，可以不写this
    fifo_thread_ = std::thread(&NodeManager::fifo_loop, this);

    RCLCPP_INFO(this->get_logger(), "FAST-LIVO2 NodeManager started.");
    RCLCPP_INFO(this->get_logger(), "Listening FIFO: %s", fifo_path_.c_str());
    RCLCPP_INFO(this->get_logger(), "Commands: start / pause / resume / stop / quit");
  }

  ~NodeManager()
  {
    running_ = false;

    stop_demo();

    if (fifo_thread_.joinable()) {//先判断线程又没有被 join 过，也没有被 detach 过 只要join 过一次，或者 detach 过，这个线程就 不再是 joinable 了，还有防止防止重复 join 导致崩溃
      fifo_thread_.join();// 主线程卡住暂停，不再往下执行，静静等子线程运行结束。线程结束后，主线程才恢复往下走
    }
  }

private:
  std::string fifo_path_;
  std::string start_fast_livo_script_;
  std::string play_bag_script_;

  std::atomic<bool> running_;//给后台线程用的一个开关标志位  running_ 表示：FIFO 监听线程要不要继续运行。

  std::thread fifo_thread_;

  pid_t fast_livo_pid_ = -1;
  pid_t bag_pid_ = -1;

private:
  void ensure_fifo()
  {
    if (access(fifo_path_.c_str(), F_OK) == 0) {
      RCLCPP_INFO(this->get_logger(), "FIFO already exists: %s", fifo_path_.c_str());
      return;
    }

    //mkfifo   创建一个FIFO命名管道文件     0666 就是    owner   group   others     6就是4 + 2 = 读 + 写  所以0666 = rw-rw-rw-    前面这个0表示是八进制的意思   返回-1是失败，0是成功
    if (mkfifo(fifo_path_.c_str(), 0666) != 0) {
      RCLCPP_ERROR(
        this->get_logger(),
        "Failed to create FIFO %s: %s",
        fifo_path_.c_str(),
        strerror(errno)
      );
    } else {
      RCLCPP_INFO(this->get_logger(), "Created FIFO: %s", fifo_path_.c_str());
    }
  }

  //后续改进方向//////////////////////////////////////////////////////////////////////
  pid_t start_process(const std::string & cmd)
  {
    pid_t pid = fork();

    if (pid < 0) {
      RCLCPP_ERROR(this->get_logger(), "fork failed: %s", strerror(errno));
      return -1;
    }

    if (pid == 0) {

      setpgid(0, 0);

      execl("/bin/bash", "bash", "-lc", cmd.c_str(), nullptr);

      // If execl returns, it failed.
      _exit(127);
    }

    // Parent process.
    setpgid(pid, pid);

    RCLCPP_INFO(this->get_logger(), "Started process pid=%d, cmd=%s", pid, cmd.c_str());
    return pid;
  }

  //后续改进方向///////////////////////////////////////////////////////////////////////////
  bool process_alive(pid_t pid)
  {
    if (pid <= 0) {
      return false;
    }

    int status = 0;
    pid_t ret = waitpid(pid, &status, WNOHANG);

    if (ret == 0) {
      return true;
    }

    return false;
  }

  //后续改进方向///////////////////////////////////////////////////////////////////////////
  void stop_process_group(pid_t & pid, const std::string & name)
  {
    if (pid <= 0) {
      return;
    }

    RCLCPP_INFO(this->get_logger(), "Stopping %s, pid=%d", name.c_str(), pid);

    // Negative pid means signal the whole process group.
    kill(-pid, SIGTERM);

    for (int i = 0; i < 20; ++i) {
      if (!process_alive(pid)) {
        pid = -1;
        RCLCPP_INFO(this->get_logger(), "%s stopped.", name.c_str());
        return;
      }
      usleep(100 * 1000);
    }

    RCLCPP_WARN(this->get_logger(), "%s did not stop, killing...", name.c_str());
    kill(-pid, SIGKILL);
    pid = -1;
  }

  void start_demo()
  {
    RCLCPP_INFO(this->get_logger(), "Command: start");

    stop_demo();

    //等旧进程停干净
    sleep(1);
  
    //no hang up 不要因为“终端挂断/关闭”就让程序退出。                                        最后面的&表示放到后台运行 
    system("nohup /home/ash/scripts/start_fast_livo.sh > /tmp/start_fast_livo.log 2>&1 &");
                                 //正常输出写到日志：> /tmp/start_fast_livo.log 等价于：1> /tmp/start_fast_livo.log  1 代表 标准输出 stdout     2>&1 :把错误输出 stderr 重定向到 标准输出 stdout 当前指向的地方
    sleep(3);

    system("nohup /home/ash/scripts/play_cbd.sh > /tmp/play_cbd.log 2>&1 &");

    RCLCPP_INFO(this->get_logger(), "Start command sent.");
  }

  // void pause_demo()
  // {
  //   RCLCPP_INFO(this->get_logger(), "Command: pause");

  //   system("pkill -STOP -f 'ros2 bag play'");

  //   RCLCPP_INFO(this->get_logger(), "Pause command sent.");
  // }

  void pause_demo()
  {
    RCLCPP_INFO(this->get_logger(), "Command: pause");

    system("pkill -STOP -f '[r]os2 bag play'");

    RCLCPP_INFO(this->get_logger(), "Pause command sent.");
  }
  // void resume_demo()
  // {
  //   RCLCPP_INFO(this->get_logger(), "Command: resume");

  //   system("pkill -CONT -f 'ros2 bag play'");

  //   RCLCPP_INFO(this->get_logger(), "Resume command sent.");
  // }
  void resume_demo()
  {
    RCLCPP_INFO(this->get_logger(), "Command: resume");

    system("pkill -CONT -f '[r]os2 bag play'");

    RCLCPP_INFO(this->get_logger(), "Resume command sent.");
  }

  // void stop_demo()
  // {
  //   RCLCPP_INFO(this->get_logger(), "Command: stop");

  //   system("pkill -CONT -f 'ros2 bag play' >/dev/null 2>&1");
  //   system("pkill -f 'ros2 bag play' >/dev/null 2>&1");
  //   system("pkill -f 'fastlivo_mapping' >/dev/null 2>&1");
  //   system("pkill -f 'mapping_aviz.launch.py' >/dev/null 2>&1");
  //   system("pkill -f 'parameter_blackboard' >/dev/null 2>&1");
  //   system("pkill -f 'image_transport' >/dev/null 2>&1");
  //   system("pkill -f 'republish' >/dev/null 2>&1");

  //   fast_livo_pid_ = -1;
  //   bag_pid_ = -1;

  //   RCLCPP_INFO(this->get_logger(), "Demo stopped.");
  // }
  void stop_demo()
  {
    RCLCPP_INFO(this->get_logger(), "Command: stop");

    system("pkill -CONT -f '[r]os2 bag play' >/dev/null 2>&1");
    system("pkill -f '[r]os2 bag play' >/dev/null 2>&1");
    system("pkill -f '[f]astlivo_mapping' >/dev/null 2>&1");
    system("pkill -f '[m]apping_aviz_lvi.launch.py' >/dev/null 2>&1");
    system("pkill -f '[p]arameter_blackboard' >/dev/null 2>&1");
    system("pkill -f '[i]mage_transport' >/dev/null 2>&1");
    system("pkill -f '[r]epublish' >/dev/null 2>&1");

    fast_livo_pid_ = -1;
    bag_pid_ = -1;

    RCLCPP_INFO(this->get_logger(), "Demo stopped.");
  }
  // void stop_demo()
  // {
  //   RCLCPP_INFO(this->get_logger(), "Command: stop");

  //   system("pkill -CONT -f '[r]os2 bag play' >/dev/null 2>&1");
  //   system("pkill -f '[r]os2 bag play' >/dev/null 2>&1");

  //   system("pkill -f '[f]astlivo_mapping' >/dev/null 2>&1");
  //   system("pkill -f '[m]apping_aviz_lvi.launch.py' >/dev/null 2>&1");
  //   system("pkill -f '[p]arameter_blackboard' >/dev/null 2>&1");

  //   system("pkill -f '[r]viz2' >/dev/null 2>&1");

  //   fast_livo_pid_ = -1;
  //   bag_pid_ = -1;

  //   RCLCPP_INFO(this->get_logger(), "Demo stopped.");
  // }


  void handle_command(const std::string & raw_cmd)
  {
    std::string cmd = raw_cmd;

    //cmd.back() 取字符串最后一个字符，注意：'\'也会被取
    //提问：这段代码的括号内执行顺序是怎么样的？？

    //如果 cmd 最后一个字符是换行符，或者是回车符，或者是空格。
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) {
      cmd.pop_back();//删除字符串最后一个字符
    }

    //判断cmd是不是空
    if (cmd.empty()) {
      return;
    }

    RCLCPP_INFO(this->get_logger(), "Received command: %s", cmd.c_str());

    if (cmd == "start") {
      start_demo();
    } else if (cmd == "pause") {
      pause_demo();
    } else if (cmd == "resume") {
      resume_demo();
    } else if (cmd == "stop") {
      stop_demo();
    } else if (cmd == "quit") {
      stop_demo();
      rclcpp::shutdown();
    } else {
      RCLCPP_WARN(this->get_logger(), "Unknown command: %s", cmd.c_str());
    }
  }

  void fifo_loop()
  {
    //int fd = open(fifo_path_.c_str(), O_RDWR);//这个是阻塞的

    //用 O_RDWR 打开 FIFO，意思是自己同时占着读端和写端，可以减少“没有写端导致读端异常退出”的问题
    //因为对于 FIFO 来说，如果只用读模式，在没有写端的时候可能会阻塞，或者在非阻塞模式下有一些 EOF 行为
    int fd = open(fifo_path_.c_str(), O_RDWR | O_NONBLOCK);

    if (fd < 0) {
      RCLCPP_ERROR(
        this->get_logger(),
        "Failed to open FIFO %s: %s",
        fifo_path_.c_str(),
        strerror(errno)
      );
      return;
    }

    //缓存变量
    std::string buffer;
    char ch;

    //只要 running_ 是 true，并且 ROS 2 还正常运行
    while (running_ && rclcpp::ok()) {

      //每次只读一个字符    read()的返回值表示这一次实际读了多少个字节
      ssize_t n = read(fd, &ch, 1);

      if (n > 0) {
        if (ch == '\n') {
          //提取字符
          handle_command(buffer);
          buffer.clear();//清的是 buffer 这个字符串里面保存的内容
        } else {
          buffer.push_back(ch);//把 ch 这个字符追加到 buffer 字符串的最后面
        }
      } else {//这次没有读到有效字符，睡眠10ms
        usleep(10 * 1000);
      }
    }

    close(fd);
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<NodeManager>();

  rclcpp::spin(node);

  rclcpp::shutdown();
  return 0;
}