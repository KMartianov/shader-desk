## PLUGIN_DEV_GUIDE.md — Руководство по созданию плагинов

### 1. Общая структура системы плагинов

Плагины в `interactive-wallpaper` — это динамически подгружаемые библиотеки (`.so`), каждая из которых реализует отдельный визуальный эффект.
Все плагины находятся в каталоге:

```
plugins/
├── ico-sphere-effect/
├── pulse-color-effect/
├── ...
└── generate_plugin.py
```

Каждый плагин — это отдельная папка с собственным:

```
CMakeLists.txt
my-effect.cpp
my-effect.hpp
shaders/
   ├── fragment.frag
   └── vertex.vert
```

Сборка выполняется автоматически при вызове `cmake ..` в корне проекта — `plugins/CMakeLists.txt` сам находит все подпапки с плагинами.

---

### 2. Автоматическая генерация плагина

Для упрощения разработки используется скрипт `plugins/generate_plugin.py`.
Он читает ваши шейдеры, анализирует комментарии с параметрами и создаёт готовые C++ файлы и `CMakeLists.txt`.

#### Пример создания нового плагина

```bash
cd plugins
mkdir my-awesome-effect
mkdir my-awesome-effect/shaders

# добавьте свои GLSL-файлы в my-awesome-effect/shaders/
# например: my-awesome-effect/shaders/effect_frag.glsl

# сгенерируйте шаблон
python3 generate_plugin.py my-awesome-effect
```

После этого в папке появятся:

```
my-awesome-effect.cpp
my-awesome-effect.hpp
CMakeLists.txt
```

Вы можете отредактировать эти файлы самостоятельно и добавить необходимый для вас функционал. 

---

### 3. Описание параметров в GLSL (аннотации @param)

`generate_plugin.py` автоматически извлекает параметры из комментариев вида:

```glsl
// @param <имя> | <тип> | <значение по умолчанию> | <описание>
```

Поддерживаемые типы:

* `float`
* `int`
* `bool`
* `vec3`

#### Пример

```glsl
// @param brightness | float | 1.0 | Общая яркость эффекта
// @param color | vec3 | 0.8,0.2,0.1 | Основной цвет
// @param use_noise | bool | true | Включить шумовой модификатор
```

Эти параметры будут автоматически:

* добавлены в C++ код как переменные и uniforms;
* доступны для настройки через `.config/interactive-wallpaper/config.json`;
* отображены в интерфейсе конфигуратора (если он используется).

---

### 4. Структура сгенерированного плагина

Сгенерированные файлы включают класс `MyAwesomeEffect`, наследующий `WallpaperEffect`.
Он реализует основные методы:

```cpp
bool initialize(uint32_t width, uint32_t height);
void render(uint32_t width, uint32_t height);
void cleanup();
void handle_audio_data(const AudioData& data);
void handle_pointer_motion(double dx, double dy, bool is_touchpad);
```

Каждый параметр, описанный через `@param`, автоматически становится:

* членом класса (например `float brightness`);
* uniform-переменной, передаваемой в шейдер.

---

### 5. Сборка плагина

После генерации достаточно пересобрать проект:

```bash
cd build
rm -rf *
cmake ..
make -j$(nproc)
```

Плагин будет собран в:

```
build/effects/
```

После успешной сборки необходимо скопировать папку effects/ в ~/.config/interactive-wallpaper/

---

### 6. Настройка и загрузка плагина

При запуске `interactive-wallpaper` ищет плагины в директории:

```
~/.config/interactive-wallpaper/effects/
```

Если вы установили программу через `make install`, плагины копируются туда автоматически.
Вы можете также вручную скопировать скомпилированный `.so` и его шейдеры.

#### Пример конфигурации (`~/.config/interactive-wallpaper/config.json`)

```json
{
  "touchpad_sensitivity": 0.0,
  "mouse_sensitivity": 0.0,
  "effect_name": "My Awesome Effect",
  "effect_settings": {
    "brightness": 1.5,
    "color": [0.2, 0.6, 1.0],
    "use_noise": false
  }
}
```

Если вы изменили effect_name, то перезапустите программу вручную.
Параметры в effect_settings должны обновляться в реальном времени сразу после сохранения. 

---

### 7. Отладка

Если плагин не появляется:

1. Проверьте, что `.so` действительно собран и лежит в `~/.config/interactive-wallpaper/effects/`.
2. Запустите приложение из терминала ( `./build/interactive-wallpaper`  ) — ошибки компиляции шейдера или загрузки будут выведены в консоль.
3. Используйте `python3 generate_plugin.py --list-params my-effect` для проверки того, что параметры парсятся корректно.

---

### 8. Рекомендации по шейдерам

* Используйте `#version 300 es` и типы `mediump` для совместимости.
* Все uniforms, влияющие на эффект, должны быть помечены через `@param`.
* Не переопределяйте `gl_FragColor`; используйте `out vec4 FragColor;`.
* Для анимации используйте uniform `float time`.

