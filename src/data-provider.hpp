#pragma once
#include "core-context.hpp"
#include "wallpaper-effect.hpp" // Подключаем для переиспользования EffectParameter и EffectParameterValue

// ==============================================================================
// ИНТЕРФЕЙС ПРОВАЙДЕРА ДАННЫХ (SMART PROVIDER)
// ==============================================================================
class IDataProvider {
public:
    virtual ~IDataProvider() = default;
    
    /**
     * @brief Человекочитаемое имя провайдера (например, "Evdev Pointer Provider")
     */
    virtual const char* get_name() const = 0;

    // --- СИСТЕМА КОНФИГУРАЦИИ (Аналогично визуальным эффектам) ---
    
    /**
     * @brief Возвращает список всех настраиваемых параметров провайдера.
     */
    virtual std::vector<EffectParameter> get_parameters() const = 0;

    /**
     * @brief Применяет параметр, загруженный из Lua конфига (init.lua).
     */
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;

    // --- ЖИЗНЕННЫЙ ЦИКЛ ---

    /**
     * @brief Инициализация плагина. Здесь он должен запросить указатели у BlackBoard 
     *        и зарегистрировать свои сокеты/таймеры в epoll.
     */
    virtual bool initialize(ICoreContext* core) = 0;
    
    virtual void cleanup() = 0;
};

// Сигнатуры для C-ABI экспорта из .so библиотек
extern "C" {
    typedef IDataProvider* (*CreateProviderFunc)();
    typedef void (*DestroyProviderFunc)(IDataProvider*);
}