# 第三方依赖来源清单

本清单记录仓库内固定第三方快照的来源、许可证和本地位置。固定版本均为完整 commit，不使用浮动 tag 或分支。

## RT-Thread

- 上游地址：<https://github.com/RT-Thread/rt-thread>
- 固定版本：`ddf52e2cdd977f14fc04035c88672ac204aec713`
- 许可证：Apache License 2.0，见 `vendor/rt-thread/LICENSE`
- 上游原始路径：仓库根目录 `/`
- 迁移前仓库路径：`rt-thread/`
- 目标路径：`vendor/rt-thread/`
- 补丁状态：无；快照内容未修改

## RT-Thread STM32 通用驱动

- 上游地址：<https://github.com/RT-Thread/rt-thread>
- 固定版本：`ddf52e2cdd977f14fc04035c88672ac204aec713`
- 许可证：Apache License 2.0，见上游根目录 `LICENSE` 和源码 SPDX 标识
- 上游原始路径：`bsp/stm32/libraries/HAL_Drivers/`
- 迁移前仓库路径：`libraries/HAL_Drivers/`
- 目标路径：`vendor/rt-thread-stm32-drivers/`
- 补丁文件：`vendor/patches/rt-thread-stm32-drivers-exti15-10-owner.patch`
- 补丁状态：已应用
- 补丁目的：当 `BSP_GPIO_EXTI15_10_EXTERNAL=y` 时不定义通用 `EXTI15_10_IRQHandler`，保留通用 `HAL_GPIO_EXTI_Callback` 并由板级 BMI088 适配器拥有中断入口
- 重放命令：`patch -d vendor/rt-thread-stm32-drivers -p1 < vendor/patches/rt-thread-stm32-drivers-exti15-10-owner.patch`

## CMSIS-Core

- 上游地址：<https://github.com/RT-Thread-packages/CMSIS-Core>
- 固定版本：`39d8e01f0be84b83a8f11d33756e82ce1ef07a84`
- 许可证：Apache License 2.0，见 `vendor/cmsis-core/LICENSE.txt`
- 上游原始路径：仓库根目录 `/`；其 README 标明核心头文件来源为 `ARM-software/CMSIS_5/CMSIS/Core/Include`
- 迁移前仓库路径：`packages/CMSIS-Core-latest/`
- 目标路径：`vendor/cmsis-core/`
- 补丁状态：无；快照内容未修改

## STM32F1 CMSIS Device

- 上游地址：<https://github.com/RT-Thread-packages/cmsis-device-f1>
- 固定版本：`4d57f5017d2937f10d07331e90828d3a81f980b8`
- 许可证：Apache License 2.0，见 `vendor/stm32f1-cmsis/License.md`
- 上游原始路径：仓库根目录 `/`
- 迁移前仓库路径：`packages/stm32f1_cmsis_driver-latest/`
- 目标路径：`vendor/stm32f1-cmsis/`
- 补丁状态：无；快照内容未修改

## STM32F1 HAL

- 上游地址：<https://github.com/RT-Thread-packages/stm32f1xx-hal-driver>
- 固定版本：`0b18f3336e7ef67e51080e72ae6805dba6cc7bb8`
- 许可证：BSD 3-Clause，见 `vendor/stm32f1-hal/LICENSE.md`
- 上游原始路径：仓库根目录 `/`
- 迁移前仓库路径：`packages/stm32f1_hal_driver-latest/`
- 目标路径：`vendor/stm32f1-hal/`
- 补丁状态：无；快照内容未修改

## STM32F103xx SVD

- 上游地址：<https://github.com/cmsis-svd/cmsis-svd-data>
- 固定版本：`c65f8551e57c770344d229dcaa0bf838fa29aff4`
- 许可证：End User License Agreement for STMicroelectronics (Version 1.0)，上游许可证路径为 `data/STMicro/License.html`
- 上游原始路径：`data/STMicro/STM32F103xx.svd`
- 迁移前仓库路径：`.vscode/STM32F103xx.svd`
- 目标路径：`.vscode/STM32F103xx.svd`
- SHA-256：`1d92b65aaf397a18a599fb6a840812015ad379cdcc0cc3687f673f63e7445367`
- 补丁状态：无；仓库文件与固定版本对应文件的 SHA-256 一致
