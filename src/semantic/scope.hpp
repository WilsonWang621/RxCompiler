#pragma once

#include "symbol.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct Scope{
private:
    enum class Namespace{
        Value, //变量、参数、函数、常量等值名字
        Type   //结构体、内建类型等类型名字
    };

    struct Binding{
        SymbolId symbol;
        std::size_t depth;
    };

    struct UndoEntry{
        Namespace nameSpace;
        std::string name;
    };

    using Bindings = std::unordered_map<std::string, std::vector<Binding>>;

    // 每个名字独立维护绑定栈，栈顶就是当前可见的符号。 名字 -> 这个名字从外到内的所有绑定
    // 两张表来分别管理
    Bindings values_;
    Bindings types_;

    // 用于每一层只记录自己引入的绑定；同层遮蔽也逐次记录。
    // 作用域深度 -> 这一层引入了哪些绑定 （这一层向哪个空间的栈里压入过绑定？）
    std::vector<std::vector<UndoEntry>> scopes_;

    static std::optional<SymbolId> lookup(const Bindings &bindings, const std::string &name){  //只要表里有就返回，不管是否在当前层
        auto it = bindings.find(name);
        if(it == bindings.end()) return std::nullopt;
        return it->second.back().symbol;
    }

    std::optional<SymbolId> lookupCurrent(const Bindings &bindings, const std::string &name) const {   //必须是当前层有才返回
        auto it = bindings.find(name);
        if(it == bindings.end() || it->second.back().depth != depth()){
            return std::nullopt;
        }
        return it->second.back().symbol;
    }

    bool declare(Bindings &bindings, Namespace nameSpace, const std::string &name, SymbolId symbol, bool allowShadowing){ //检查重名和同步更新绑定和日志
        //拒绝非法输入
        if(name.empty() || symbol.value == 0) return false;

        //检查同层重名   allowShadowing : allow same-scope shadowing/rebinding
        if(!allowShadowing && lookupCurrent(bindings, name).has_value()){
            return false;
        }

        //记录撤销日志 退出作用域时就根据这些记录撤销绑定
        scopes_.back().push_back({nameSpace, name});
        try{
            bindings[name].push_back({symbol, depth()});  //压入名字对应的绑定栈
        }catch(...){
            // 分配失败时撤销日志和可能新增的空栈，保持查找与退出操作一致。
            scopes_.back().pop_back();
            auto it = bindings.find(name);
            if(it != bindings.end() && it->second.empty()) bindings.erase(it);
            throw;
        }
        return true;
    }

public:
    // 初始保留全局作用域，depth 为 0。
    Scope(): scopes_(1) {}

    std::size_t depth() const { return scopes_.size() - 1; }

    void pushScope(){ //建立一层新的作用域
        scopes_.emplace_back();
    }

    bool popScope(){ // 全局顶层永远不能弹出；成功退出时返回 true。
        if(scopes_.size() == 1) return false;
        //逆序遍历当前层的撤销日志
        for(auto it = scopes_.back().rbegin(); it != scopes_.back().rend(); ++it){
            auto &bindings = it->nameSpace == Namespace::Value ? values_ : types_;
            auto binding = bindings.find(it->name);
            binding->second.pop_back();
            if(binding->second.empty()) bindings.erase(binding);
        }
        scopes_.pop_back();
        return true;
    }

    // 默认拒绝同层重名；let 遮蔽时显式传入 true。
    // 是否能遮蔽常量或受保护内建名，由语义检查器决定。
    bool declareValue(const std::string &name, SymbolId symbol, bool allowShadowing = false){
        return declare(values_, Namespace::Value, name, symbol, allowShadowing);
    }

    bool declareType(const std::string &name, SymbolId symbol, bool allowShadowing = false){
        return declare(types_, Namespace::Type, name, symbol, allowShadowing);
    }

    // 查找仅访问对应名字的栈顶，不遍历作用域层级。
    std::optional<SymbolId> lookupValue(const std::string &name) const {
        return lookup(values_, name);
    }

    std::optional<SymbolId> lookupType(const std::string &name) const {
        return lookup(types_, name);
    }

    std::optional<SymbolId> lookupCurrentValue(const std::string &name) const {
        return lookupCurrent(values_, name);
    }

    std::optional<SymbolId> lookupCurrentType(const std::string &name) const {
        return lookupCurrent(types_, name);
    }
};

// RAII 自动管理作用域
class ScopeGuard {
    Scope *scope_;

public:
    explicit ScopeGuard(Scope &scope): scope_(&scope) {
        scope_->pushScope();
    }

    ~ScopeGuard() {
        if (scope_ != nullptr) {
            scope_->popScope();
        }
    }

    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator=(const ScopeGuard &) = delete;

    ScopeGuard(ScopeGuard &&other) noexcept: scope_(other.scope_) {
        other.scope_ = nullptr;
    }

    ScopeGuard &operator=(ScopeGuard &&) = delete;
};
