PROJECT_DIR := $(CURDIR)
TARGET ?= esp32c3
SERIAL_PORT_HELPER := $(PROJECT_DIR)/tools/detect_serial_port.sh
PORT ?= $(shell if [ -x "$(SERIAL_PORT_HELPER)" ]; then "$(SERIAL_PORT_HELPER)" --quiet 2>/dev/null || true; fi)
PORT_SOURCE ?= $(shell if [ -x "$(SERIAL_PORT_HELPER)" ]; then "$(SERIAL_PORT_HELPER)" --quiet --source 2>/dev/null || true; fi)
BAUD ?= 460800
MONITOR_BAUD ?= 115200
IDF_PY ?= idf.py
ESPTOOL_PY ?= python -m esptool
APP_OFFSET ?= 0x10000

.PHONY: help build reconfigure fullclean clean flash app-flash bootloader-flash partition-flash erase erase-flash erase-app monitor log flash-monitor run reboot size menuconfig set-target port check-port doctor

help:
	@printf '%s\n' \
	  'ESP32-C3 WirelessID 常用命令:' \
	  '  make build          - 编译工程' \
	  '  make flash          - 烧录整个工程' \
	  '  make monitor        - 打开串口日志' \
	  '  make log            - monitor 的快捷别名' \
	  '  make flash-monitor  - 先烧录，再按正确日志波特率打开串口' \
	  '  make run            - build + flash + monitor 一键运行' \
	  '  make reboot         - 通过串口触发芯片复位' \
	  '  make erase-flash    - 整片擦除 Flash' \
	  '  make erase-app      - 仅擦除应用分区起始区域' \
	  '  make doctor         - 检查串口、工具链、构建产物与 Flash 参数' \
	  '  make clean          - 清理当前构建产物' \
	  '  make fullclean      - 深度清理构建目录' \
	  '  make menuconfig     - 打开 ESP-IDF 配置菜单' \
	  '  make size           - 查看固件体积信息' \
	  '  make app-flash      - 仅烧录应用分区' \
	  '  make bootloader-flash - 仅烧录 bootloader' \
	  '  make partition-flash  - 仅烧录分区表' \
	  '  make reconfigure    - 重新配置工程' \
	  '  make set-target     - 显式设置芯片为当前 TARGET' \
	  '  make check-port     - 检查当前串口路径是否存在' \
	  '  make port           - 打印当前串口配置' \
	  '' \
	  '退出串口日志:' \
	  '  Ctrl+]              - 退出 idf monitor' \
	  '' \
	  '可覆盖变量示例:' \
	  '  make flash TARGET=$(TARGET) PORT=/host-dev/serial/by-id/your-device' \
	  '  make run TARGET=$(TARGET) BAUD=921600 MONITOR_BAUD=115200' \
	  '  make erase-app TARGET=esp32c6 APP_OFFSET=0x10000'

port:
	@printf 'PORT=%s\nBAUD=%s\nMONITOR_BAUD=%s\n' "$(PORT)" "$(BAUD)" "$(MONITOR_BAUD)"
	@printf 'PORT_SOURCE=%s\n' "$(PORT_SOURCE)"
	@if [ -x "$(SERIAL_PORT_HELPER)" ]; then \
		printf '\nAvailable serial candidates:\n'; \
		"$(SERIAL_PORT_HELPER)" --list || true; \
	fi

check-port:
	@if [ -z "$(PORT)" ]; then \
		if [ -x "$(SERIAL_PORT_HELPER)" ]; then \
			"$(SERIAL_PORT_HELPER)"; \
		else \
			printf 'Serial port helper missing: %s\n' "$(SERIAL_PORT_HELPER)"; \
			exit 1; \
		fi; \
	fi
	@test -e "$(PORT)" || (printf 'Serial port not found: %s\n' "$(PORT)" && exit 1)
	@printf 'Serial port ready: %s\n' "$(PORT)"

build:
	$(IDF_PY) build

reconfigure:
	$(IDF_PY) reconfigure

fullclean:
	$(IDF_PY) fullclean

clean:
	$(IDF_PY) clean

flash: check-port
	$(IDF_PY) -p "$(PORT)" -b $(BAUD) flash

app-flash: check-port
	$(IDF_PY) -p "$(PORT)" -b $(BAUD) app-flash

bootloader-flash: check-port
	$(IDF_PY) -p "$(PORT)" -b $(BAUD) bootloader-flash

partition-flash: check-port
	$(IDF_PY) -p "$(PORT)" -b $(BAUD) partition-table-flash

erase:
	$(IDF_PY) erase-flash

erase-flash: check-port
	$(IDF_PY) -p "$(PORT)" erase-flash

monitor: check-port
	$(IDF_PY) -p "$(PORT)" monitor -b $(MONITOR_BAUD)

log: monitor

flash-monitor: check-port
	$(IDF_PY) -p "$(PORT)" -b $(BAUD) flash
	$(IDF_PY) -p "$(PORT)" monitor -b $(MONITOR_BAUD)

run: build flash-monitor

reboot: check-port
	$(ESPTOOL_PY) --chip $(TARGET) -p "$(PORT)" run

erase-app: check-port
	$(ESPTOOL_PY) --chip $(TARGET) -p "$(PORT)" erase_region $(APP_OFFSET) 0x100000

size:
	$(IDF_PY) size

menuconfig:
	$(IDF_PY) menuconfig

set-target:
	$(IDF_PY) set-target $(TARGET)

doctor:
	@printf '== ESP32 doctor ==\n'
	@printf 'PROJECT_DIR=%s\n' "$(PROJECT_DIR)"
	@printf 'TARGET=%s\n' "$(TARGET)"
	@printf 'PORT=%s\nPORT_SOURCE=%s\nBAUD=%s\nMONITOR_BAUD=%s\nAPP_OFFSET=%s\n' "$(PORT)" "$(PORT_SOURCE)" "$(BAUD)" "$(MONITOR_BAUD)" "$(APP_OFFSET)"
	@printf '\n[1/6] Check serial port\n'
	@test -e "$(PORT)" && printf '  OK: serial port exists\n' || (printf '  FAIL: serial port not found\n' && exit 1)
	@printf '\n[2/6] Check idf.py\n'
	@$(IDF_PY) --version
	@printf '\n[3/6] Check esptool\n'
	@$(ESPTOOL_PY) version
	@printf '\n[4/6] Check build directory\n'
	@test -d build && printf '  OK: build directory exists\n' || (printf '  WARN: build directory missing; run make build\n' && exit 1)
	@printf '\n[5/6] Check key artifacts\n'
	@test -f build/WirelessID.bin && printf '  OK: build/WirelessID.bin\n' || (printf '  FAIL: missing build/WirelessID.bin\n' && exit 1)
	@test -f build/bootloader/bootloader.bin && printf '  OK: build/bootloader/bootloader.bin\n' || (printf '  FAIL: missing build/bootloader/bootloader.bin\n' && exit 1)
	@test -f build/partition_table/partition-table.bin && printf '  OK: build/partition_table/partition-table.bin\n' || (printf '  FAIL: missing build/partition_table/partition-table.bin\n' && exit 1)
	@printf '\n[6/6] Check flash args\n'
	@test -f build/flash_args && grep -q -- '--flash_size 4MB' build/flash_args && printf '  OK: flash args set to 4MB\n' || (printf '  FAIL: build/flash_args missing or flash size mismatch\n' && exit 1)
	@printf '\nMonitor note: flash baud=%s, monitor baud=%s\n' "$(BAUD)" "$(MONITOR_BAUD)"
	@printf '\nDoctor summary: environment looks ready for build/flash/monitor.\n'
