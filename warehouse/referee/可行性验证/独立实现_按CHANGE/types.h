#pragma once
#include <string>

// 机器人状态：IDLE / TO_PICKUP / DELIVERING / TO_CHARGER / CHARGING / DEAD
struct Robot {
    int id = 0;
    std::string name;            // 形如 "R12"
    int x = 0, y = 0;
    int battery = 0;
    std::string state = "IDLE";
    std::string orderId;         // 当前订单编号，空串表示没有
    int tx = -1, ty = -1;        // 当前目标格
    int waitStreak = 0;          // 连续等待次数
    long long waits = 0;         // 累计等待次数
    long long travelled = 0;     // 累计移动步数
    int deadSince = -1;          // 停机的 tick
};

// 订单状态：PENDING / ASSIGNED / PICKED / DONE / CANCELLED / LOST
struct Order {
    std::string id;
    int px = 0, py = 0;          // 取货点
    int dx = 0, dy = 0;          // 送货点
    int priority = 0;
    int arrival = 0;             // 到达的 tick
    std::string status = "PENDING";
    std::string robot;           // 负责的机器人名字
};

struct Params {
    int batteryMax = 0;
    int chargeRate = 0;
    int lowThreshold = 0;
    int safetyMargin = 0;
    int reportEvery = 0;
    int hotRadius = 0;
    int agingEvery = 1;
    int carryCost = 1;
};
