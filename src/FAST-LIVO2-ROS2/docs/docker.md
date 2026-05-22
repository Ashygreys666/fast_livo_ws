# Docker Guide

This guide describes how to build the FAST-LIVO2 Docker image and run a
pre-recorded ROS 2 bag through it.

The `Dockerfile` lives at the repository root and produces a self-contained
ROS 2 Humble image with FAST-LIVO2 and all its non-system dependencies
**pre-built** — no `colcon build` or `source setup.bash` is needed inside
the container. The entrypoint sources `/opt/ros/humble/setup.bash` and
`/fast_livo_ws/install/setup.bash` for you before running any command.

---

## 1. What's in the image

| Component            | Source                                                                  | Build location            |
|----------------------|-------------------------------------------------------------------------|---------------------------|
| ROS 2 Humble base    | `ros:humble-perception-jammy`                                           | apt                       |
| FAST-LIVO2           | this repository (copied into image via `COPY .`)                        | colcon workspace          |
| Livox-SDK2           | <https://github.com/Livox-SDK/Livox-SDK2>                               | installed to `/usr/local` |
| livox_ros_driver2    | <https://github.com/Livox-SDK/livox_ros_driver2>                        | colcon workspace          |
| rpg_vikit (U-AMC)    | <https://github.com/U-AMC/rpg_vikit_rational_polynomial>                | `vikit_common` to `/usr/local`, `vikit_ros` via colcon |
| Sophus, PCL, OpenCV  | apt (`ros-humble-sophus`, `libpcl-dev`, `libopencv-dev`)                | apt                       |

Workspace path inside the container: `/fast_livo_ws`
(sourced automatically by the entrypoint).

The image bakes in launch files for both pipelines — LiDAR-Inertial only
(`mapping_aviz.launch.py`, `mapping_mid360.launch.py`) and
LiDAR-Visual-Inertial (`mapping_aviz_lvi.launch.py`,
`mapping_mid360_lvi.launch.py`). See the README for launch commands.

---

## 2. Build the image

From the repository root:

```bash
docker build -t fast-livo2:humble .
```

Override the ROS distribution if needed (only Humble is regularly tested):

```bash
docker build --build-arg ROS_DISTRO=humble -t fast-livo2:humble .
```

The build is fully unauthenticated — every dependency is fetched over HTTPS,
so no SSH key or credential setup is required.

---

## 3. Run a ROS 2 bag through the image

The image already contains a fully built workspace, so running a bag is just
"mount the bag, launch the mapper, play the bag." Two patterns are shown
below — pick the one that matches whether you want everything inside one
container or to play the bag from the host.

Pick the pipeline you want to run:

| LiDAR        | LiDAR-Inertial only            | LiDAR-Visual-Inertial               |
|--------------|--------------------------------|-------------------------------------|
| Livox Avia   | `mapping_aviz.launch.py`       | `mapping_aviz_lvi.launch.py`        |
| Livox Mid-360| `mapping_mid360.launch.py`     | `mapping_mid360_lvi.launch.py`      |

> If your bag was recorded with the old `livox_ros_driver/msg/CustomMsg`
> type, patch its `metadata.yaml` to use `livox_ros_driver2/msg/CustomMsg`
> first (see the README, §4.2). The image only builds the v2 message.

### 3.1 Everything inside one container (recommended)

Mount the directory that holds your bag and run both the mapper and
`ros2 bag play` in the same container. This avoids any host ⇄ container DDS
discovery concerns.

```bash
docker run --rm -it \
  -v /absolute/path/to/bags:/bags:ro \
  fast-livo2:humble \
  bash -lc '
    ros2 launch fast_livo mapping_mid360_lvi.launch.py use_rviz:=False &
    sleep 3
    ros2 bag play -p /bags/your_bag_dir
  '
```

- Replace `/absolute/path/to/bags` with the host directory containing your
  bag folder (the directory `your_bag_dir`, not the `.db3` file).
- Replace `mapping_mid360_lvi.launch.py` with the launch file you want.
- The `sleep 3` gives the launch tree time to come up before bag playback
  starts.

### 3.2 Bag on the host, mapper in a container

Useful when the bag is being streamed by something on the host, or you want
to drive playback interactively. Requires `--net=host` so both processes see
the same DDS traffic.

**Terminal A** — start the mapper in a container:

```bash
docker run --rm -it --net=host fast-livo2:humble \
  ros2 launch fast_livo mapping_mid360_lvi.launch.py use_rviz:=False
```

**Terminal B** — play the bag on the host (needs ROS 2 Humble installed):

```bash
ros2 bag play -p /absolute/path/to/bags/your_bag_dir
```

Both processes must share the same `ROS_DOMAIN_ID` (defaults to `0`).

---

## 4. RViz from the container

To get RViz rendered on your host display, forward X11 into the container:

```bash
xhost +local:docker        # allow local docker clients (run on host)

docker run --rm -it \
  --net=host \
  --env DISPLAY=$DISPLAY \
  --volume /tmp/.X11-unix:/tmp/.X11-unix \
  -v /absolute/path/to/bags:/bags:ro \
  fast-livo2:humble \
  bash -lc '
    ros2 launch fast_livo mapping_mid360_lvi.launch.py use_rviz:=True &
    sleep 3
    ros2 bag play -p /bags/your_bag_dir
  '
```

Revoke access when done: `xhost -local:docker`.

For NVIDIA GPUs, also add `--gpus all` and ensure the NVIDIA Container
Toolkit is installed on the host.

---

## 5. Persisting outputs

FAST-LIVO2 writes point clouds, images, trajectories, and COLMAP output to
`/fast_livo_ws/src/fast_livo/Log/` inside the container. To keep them, mount
a host directory at that path:

```bash
docker run --rm -it \
  -v /absolute/path/to/bags:/bags:ro \
  -v $PWD/Log:/fast_livo_ws/src/fast_livo/Log \
  fast-livo2:humble \
  bash -lc '
    ros2 launch fast_livo mapping_mid360_lvi.launch.py use_rviz:=False &
    sleep 3
    ros2 bag play -p /bags/your_bag_dir
  '
```

`$PWD/Log` on the host will hold `pcd/`, `image/`, `result/`, and
`Colmap/` after the run.
