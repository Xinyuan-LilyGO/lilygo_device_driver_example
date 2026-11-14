/*
 * @Description: 惯性传感器九轴采样数据测试入口声明
 */
#pragma once

// 由所选开发板对应的实现提供统一入口。
void RunImuExample();

/**
 * @brief 输出未校准的 XY 平面磁场角度，+X 为 0 度，向 +Y 增加
 * @param x X 轴磁场，单位 uT
 * @param y Y 轴磁场，单位 uT
 * @param ready 磁场样本是否有效且未超时
 */
void PrintImuMagneticAngle(float x, float y, bool ready);
