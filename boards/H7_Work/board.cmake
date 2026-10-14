# keep first
board_runner_args(jlink "--device=STM32H723VG" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

board_runner_args(pyocd "--target=stm32h723xx" "--frequency=1000000")
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)