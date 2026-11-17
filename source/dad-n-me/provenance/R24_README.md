# 狂扁小朋友 Response R24

PSP-2000/3000本地测试候选。原作流程的软件回归已通过，复杂战斗实机速度、输入延迟和全部偶发无法移动原因尚未验收。详见验证报告.md和qa。

方向键/摇杆移动，×轻攻击、□重攻击、START暂停，结局或失败界面×重开，HOME返回系统。原作30Hz逻辑，约59.94Hz插值显示目标。

构建：`python3 build.py --pspdev /path/to/pspdev --cmake /path/to/cmake --build-dir /path/to/output`。
TESTED_IDENTITY.json绑定源码、素材和测试程序，REBUILD.json记录独立重建。素材完整随附，使用GCC15.2/PSPSDK兼容工具链。qa方法中的工作区路径需调整；PPSSPP QA应用另外提供。qa/fixtures的高血量和故障SWF不属于普通安装内容。
