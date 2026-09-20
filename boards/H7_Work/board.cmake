# keep first
board_runner_args(pyocd "--target=stm32h723xx" "--frequency=1000000")

# keep first
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)