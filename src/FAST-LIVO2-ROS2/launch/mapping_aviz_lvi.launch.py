#!/usr/bin/python3
# -- coding: utf-8 --**

import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from ament_index_python.packages import get_package_share_directory
from launch_ros.actions import Node

def generate_launch_description():
    
    # Find path     先找到 fast_livo 这个 ROS2 包的安装目录，然后在里面找：下面这两个文件
    config_file_dir = os.path.join(get_package_share_directory("fast_livo"), "config")
    rviz_config_file = os.path.join(get_package_share_directory("fast_livo"), "rviz_cfg", "fast_livo2.rviz")

    #Load parameters      指定两个默认参数文件
    avia_config_cmd = os.path.join(config_file_dir, "avia_lvi.yaml")
    camera_config_cmd = os.path.join(config_file_dir, "camera_pinhole.yaml")##############################################

    # Param use_rviz  声明启动时可以改的参数  所以默认不启动 RViz
    use_rviz_arg = DeclareLaunchArgument(
        "use_rviz",
        default_value="False",
        description="Whether to launch Rviz2",
    )

    #意思是声明一个参数：avia_params_file  默认使用config/avia_lvi.yaml 也可以启动时换成别的配置文件：ros2 launch fast_livo mapping_aviz_lvi.launch.py avia_params_file:=/path/to/your.yaml
    avia_config_arg = DeclareLaunchArgument(
        'avia_params_file',
        default_value=avia_config_cmd,
        description='Full path to the ROS2 parameters file to use for fast_livo2 nodes',
    )

    #声明相机参数文件
    camera_config_arg = DeclareLaunchArgument(
        'camera_params_file',
        default_value=camera_config_cmd,
        description='Full path to the ROS2 parameters file to use for vikit_ros nodes',
    )

    # https://github.com/ros-navigation/navigation2/blob/1c68c212db01f9f75fcb8263a0fbb5dfa711bdea/nav2_bringup/launch/navigation_launch.py#L40
    #如果某个节点崩了，自动重启
    use_respawn_arg = DeclareLaunchArgument(
        'use_respawn', 
        default_value='True',
        description='Whether to respawn if a node crashes. Applied when composition is disabled.')

    #读取刚才声明的参数      avia_params_file = 启动时传进来的 avia_params_file
    avia_params_file = LaunchConfiguration('avia_params_file')
    camera_params_file = LaunchConfiguration('camera_params_file')
    use_respawn = LaunchConfiguration('use_respawn')

    #真正开始列启动任务
    return LaunchDescription([
        use_rviz_arg,
        avia_config_arg,
        camera_config_arg,
        use_respawn_arg,

        # play ros2 bag
        # ExecuteProcess(
        #     cmd=[['ros2 bag play ', '~/datasets/Retail_Street ', '--clock ', "-l"]], 
        #     shell=True
        # ),

        # use parameter_blackboard as global parameters server and load camera params
        Node(
            package='demo_nodes_cpp',    #创建一个 “公共参数仓库”，把相机参数放进去，让别的节点来拿。
            executable='parameter_blackboard',
            name='parameter_blackboard',
            # namespace='laserMapping',
            parameters=[
                camera_params_file,
            ],
            output='screen'
        ),

        # republish compressed image to raw image
        # https://robotics.stackexchange.com/questions/110939/how-do-i-remap-compressed-video-to-raw-video-in-ros2
        # ros2 run image_transport republish compressed raw --ros-args --remap in:=/left_camera/image --remap out:=/left_camera/image
        Node(
            package="image_transport",#把压缩图像重新发布成原始图像
            executable="republish",
            name="republish",
            arguments=[ # Array of strings/parametric arguments that will end up in process's argv     输入格式是 compressed，输出格式是 raw  订阅：/left_camera/image/compressed 发布：/left_camera/image
                'compressed', 
                'raw',
            ],
            remappings=[
                ("in",  "/left_camera/image"), 
                ("out", "/left_camera/image")
            ],
            output="screen",
            respawn=use_respawn,
        ),
        
        Node(
            package="fast_livo",   #主算法节点
            executable="fastlivo_mapping",
            name="laserMapping",
            parameters=[
                avia_params_file,   #貌似这里只有雷达参数
                camera_params_file,#我新加的
            ],
            # https://docs.ros.org/en/humble/How-To-Guides/Getting-Backtraces-in-ROS-2.html
            prefix=[
                # ("gdb -ex run --args"),
                # ("valgrind --log-file=./valgrind_report.log --tool=memcheck --leak-check=full --show-leak-kinds=all -s --track-origins=yes --show-reachable=yes --undef-value-errors=yes --track-fds=yes")
            ],
            output="screen"
        ),

        Node(
            condition=IfCondition(LaunchConfiguration("use_rviz")),#RViz2 可视化  默认关闭
            package="rviz2",
            executable="rviz2",
            name="rviz2",
            arguments=["-d", rviz_config_file],
            output="screen"
        ),
    ])
