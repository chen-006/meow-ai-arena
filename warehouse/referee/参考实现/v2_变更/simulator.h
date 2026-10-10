#pragma once
#include <list>
#include <map>
#include <string>
#include <vector>

#include "eventlog.h"
#include "grid.h"
#include "types.h"

class Simulator {
public:
    // 从输入流读取整个场景
    void load(std::istream& in);

    // 运行全部 tick 并输出结果
    void run();

private:
    Grid grid;
    Params params;
    int totalTicks = 0;
    std::vector<Robot> robots;
    std::map<std::string, Order> orders;      // 按订单编号查找
    std::list<std::string> pending;           // 等待分配的订单编号
    std::vector<std::string> eventLines;      // 原始事件行，形如 "12 ORDER ..."
    EventLog log;

    long long delivered = 0;
    long long lost = 0;
    long long rejected = 0;
    long long cancelled = 0;
    long long latencySum = 0;
    long long latencyMax = 0;

    void applyEvents(int tick);
    void handleOrder(int tick, std::istringstream& args);
    void handleCancel(int tick, std::istringstream& args);

    void rescueStep(int tick);
    void chargeStep(int tick);
    void dispatch(int tick);
    void sendLowBatteryToCharge(int tick);
    std::pair<int, int> chooseCharger(const Robot& r);
    void moveRobots(int tick);
    void moveOne(int tick, Robot& r);
    void arrive(int tick, Robot& r);
    void killRobot(int tick, Robot& r);
    void report(int tick);
    void summary();
    int effectivePriority(const Order& o, int tick) const;
    int stepCost(const Robot& r) const;

    Robot* findRobotByName(const std::string& name);
    bool occupied(int x, int y, int exceptId) const;
    bool isMovingState(const std::string& state) const;
    void removePending(const std::string& id);
};
