// src/wallpaper-effect.hpp
#pragma once

#include <vector>
#include <string>
#include <variant>
#include <memory>
#include <glm/glm.hpp>

// Подключаем интерфейс Ядра (для доступа к BlackBoard)
#include "core-context.hpp" 

class WallpaperEffect;

// Типы данных, которые можно настраивать через config.json
using EffectParameterValue = std::variant<bool, int, float, glm::vec3, std::string>;

// Умный указатель с кастомным удалителем (необходимо для безопасной выгрузки .so библиотек)
using WallpaperEffectPtr = std::unique_ptr<WallpaperEffect, void(*)(WallpaperEffect*)>;

// Структура, описывающая один настраиваемый параметр плагина
struct EffectParameter {
    std::string name;        // Уникальное имя (совпадает с именем в JSON)
    std::string description; // Описание (для потенциального GUI)
    EffectParameterValue value; // Текущее/дефолтное значение
};

// ==============================================================================
// ИНТЕРФЕЙС ВИЗУАЛЬНОГО ПЛАГИНА (ЭФФЕКТА)
// ==============================================================================
class WallpaperEffect {
public:
    virtual ~WallpaperEffect() = default;

    // --------------------------------------------------------------------------
    // 1. ЖИЗНЕННЫЙ ЦИКЛ И РЕНДЕРИНГ
    // --------------------------------------------------------------------------

    /**
     * @brief Инициализация плагина.
     * @param core Указатель на контекст Ядра. Здесь плагин должен запросить
     *             сырые указатели на нужные ему данные из BlackBoard.
     *             Пример: p_bass = core->get_blackboard().bind_float("audio.bass");
     * @param width Ширина экрана
     * @param height Высота экрана
     * @return true в случае успеха, false при ошибке (например, не скомпилировался шейдер)
     */
    virtual bool initialize(ICoreContext* core, uint32_t width, uint32_t height) = 0;

    /**
     * @brief Отрисовка кадра. Вызывается Ядром каждый кадр (синхронизировано с Wayland VSync).
     *        Здесь плагин должен прочитать значения по полученным ранее указателям
     *        (например, *p_bass) и передать их в OpenGL (glUniform1f).
     * @param width Текущая ширина окна (может меняться при ресайзе)
     * @param height Текущая высота окна
     */
    virtual void render(uint32_t width, uint32_t height) = 0;

    /**
     * @brief Очистка ресурсов. Вызывается перед выгрузкой плагина или уничтожением окна.
     *        Здесь нужно удалять OpenGL объекты (glDeleteProgram, glDeleteBuffers).
     */
    virtual void cleanup() = 0;

    // ВАЖНО: Обработчики handle_pointer_motion и handle_audio_data удалены!
    // Архитектура теперь Data-Driven. Эффект сам читает нужные числа из BlackBoard
    // во время вызова render().

    // --------------------------------------------------------------------------
    // 2. СИСТЕМА КОНФИГУРАЦИИ (JSON)
    // --------------------------------------------------------------------------

    /**
     * @brief Возвращает человекочитаемое имя эффекта.
     */
    virtual const char* get_name() const = 0;

    /**
     * @brief Возвращает список всех параметров эффекта и их значения по умолчанию.
     *        Используется утилитой plugin-interrogator для генерации конфигов.
     */
    virtual std::vector<EffectParameter> get_parameters() const = 0;

    /**
     * @brief Применяет параметр, загруженный из config.json.
     * @param name Имя параметра
     * @param value Значение параметра
     */
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;
};

// ==============================================================================
// ФУНКЦИИ ЭКСПОРТА (Обязательны для каждого плагина)
// ==============================================================================
extern "C" {
    // Фабричный метод: создает экземпляр эффекта
    WallpaperEffect* create_effect();
    
    // Метод уничтожения: корректно удаляет экземпляр, выделенный внутри .so
    void destroy_effect(WallpaperEffect* effect);
}