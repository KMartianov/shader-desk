#pragma once
#include "core-context.hpp"

class IDataProvider {
public:
    virtual ~IDataProvider() = default;
    
    // Инициализация плагина. Здесь он должен запросить указатели у BlackBoard 
    // и зарегистрировать свои сокеты/таймеры в epoll.
    virtual bool initialize(ICoreContext* core) = 0;
    
    virtual void cleanup() = 0;
    virtual const char* get_name() const = 0;
};

// Сигнатуры для экспорта из .so
extern "C" {
    typedef IDataProvider* (*CreateProviderFunc)();
    typedef void (*DestroyProviderFunc)(IDataProvider*);
}