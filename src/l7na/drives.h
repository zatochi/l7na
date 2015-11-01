#include <cstdint>
#include <atomic>
#include <memory>

/*! @brief API системы управления двигателями метеорологической антенны ДМРЛ-3
 *
 *  Описание работы с системой управления.
 *
 *  До начала работы с системой управления ее состояние описывается константой STATE_OFF.
 *
 *  Для начала работы необходимо вызывать функцию Init(config_file_path).
 *  Синтаксис конфигурационного файла - набор строчек вида:
 *  @code{.unparsed}
 *  60F7=35     // Комментарий 1
 *  6083=20000  // Комментарий 2
 *  # Комментарий 3
 *  6084=20000
 *  ...
 *  @endcode
 *  где поле слева от '=' - адрес регистра, а справа - значение, которое туда нужно записать
 *  при инициализации системы.
 *
 *  После успешной инициализации система переходит в статут STATE_IDLE.
 *
 *  Далее возможен переход в один из основных режимов работы - сканирование по азимуту или
 *  позиционирование в точку. Для каждого режима выделена отдельная функция. Состояние системы описывается одной
 *  из констант [STATE_SCAN, STATE_POINT].
 *
 *  Получение текущих значений для обоих осей осуществляется вызов функции GetStatus,
 *  возвращающей развернутый текущий статус системы.
 *
 *  При возникновении ошибки состояние системы становится STATE_ERROR. Поле error_code для двигателя, вызвавшего ошибку
 *  установлено в соответствующее значение. Для продолжения работы необходимо сначала перевести систему в STATE_IDLE
 *  соответствующим вызовом.
 *
 *  При завершении работы необходимо из любого режима вызвать метод Release(), который остановит двигатели
 *  и произведет необходимую деинициализацию.
 */

namespace Drives {

//! @brief Возможные состояния каждого двигателя
enum class AxisState : int32_t {
    AXIS_OFF = -1,                   //!< Выключен (до вызова Init или после вызова Release)
    AXIS_INIT,                      //!< Инициализируется
    AXIS_IDLE,                      //!< Включен, готов к работе
    AXIS_SCAN,                      //!< Работает в режиме "Сканирование"
    AXIS_POINT,                     //!< Работает в режиме "Установка в точку"
    AXIS_ERROR                      //!< Состояние ошибки
};

//! @brief Текущие значения для одной оси системы
struct AxisStatus {
    AxisStatus() noexcept
        : target_position(0)
        , cur_position(0)
        , demand_position(0)
        , target_velocity(0)
        , cur_velocity(0)
        , demand_velocity(0)
        , cur_torque(0)
        , state(AxisState::AXIS_OFF)
        , error_code(0)
    {}

    int32_t     target_position;        //!< Целевая позиция [импульсы энкодера]
    int32_t     cur_position;           //!< Текущая позиция [импульсы энкодера]
    int32_t     demand_position;        //!< Запрашиваемая позиция [импульсы энкодера]
    int32_t     target_velocity;        //!< Целевая скорость [импульсы энкодера/с]
    int32_t     cur_velocity;           //!< Текущая скорость [импульсы энкодера/с]
    int32_t     demand_velocity;        //!< Запрашиваемая скорость [импульсы энкодера/c]
    int32_t     cur_torque;             //!< Текущий момент [единиц 0,1% от номинального момента двигателя]
    AxisState   state;                  //!< Текущее состояние системы управления осью
    uint32_t    error_code;             //!< Код ошибки двигателя по CiA402
    uint16_t    statusword;             //!< Битовая маска текущего состояния привода
};

enum class SystemState : int32_t {
    SYSTEM_OFF = -1,
    SYSTEM_OK = 0,
    SYSTEM_INIT,
    SYSTEM_ERROR
};

//! @brief Текущие значения, возвращаемые системой управления
struct SystemStatus {
    SystemStatus() noexcept
        : azimuth()
        , elevation()
        , state(SystemState::SYSTEM_OFF)
        , error_str()
    {}

    AxisStatus azimuth;             //!< Статус двигателя по азимуту
    AxisStatus elevation;           //!< Статус двигателя по углу места
    SystemState state;              //!< Состояние системы
    std::string error_str;          //!< Описание ошибки или пустая строка
};

//! @brief Статическая информация для одной оси. Заполняется один раз при инициализации.
struct AxisInfo {
    AxisInfo() noexcept
        : encoder_type(4)
        , cur_temperature(0)
        , dev_name()
        , hw_version()
        , sw_version()
    {}

    /*! Тип энкодера.
     * 0 - unknown
     * 1 - Serial type encoder (-)
     * 2 - Serial type Abs encoder (12-bit)
     * 3 - Serial type Abs encoder (16-bit)
     * 4 - Serial type Abs encoder (20-bit) <-- сейчас используется этот.
     * 5 - Serial type Abs encoder (24-bit)
     */
    uint16_t        encoder_type;
    int32_t         cur_temperature;    //!< Текущая температура для сервоусилителя
    std::string     dev_name;           //!< Название устройства
    std::string     hw_version;         //!< Версия аппаратного обепечения
    std::string     sw_version;         //!< Версия программного обеспечения
};

//! @brief Статическая информация для системы
struct SystemInfo {
    SystemInfo() noexcept
        : azimuth()
        , elevation()
    {}

    AxisInfo azimuth;
    AxisInfo elevation;
};

/*! @brief Объект управления системой вращения.
 *
 *  Созданием объекта == подлключение к системе управления
 *
 */
class Control {
public:
    /*! @brief Конструктор. Инициализирует систему управления.
     *
     *  @param   cfg_file_path  Путь к файлу с конфигурацией системы (абсолютный или относительно текущей рабочей директории)
     */
    Control(const char* cfg_file_path);

    /*! @brief Деструктор. Приводит систему управления в первоначальное состояние/выключает систему управления.
     */
    ~Control();

    /*! @brief В зависимости от значений параметров, задает разные режимы работы двигателей по осям (позиционирование в точку или сканирование).
     *
     *  Соответственно для каждой оси:
     *  if (velocity == 0) {
     *      Позиционирование в точку;
     *  } else {
     *      Вращение с указанной скоростью;
     *  }
     *
     *  Для азимута:
     *  if (velocity > 0) {
     *      Вращение _по_ часовой стрелке;
     *  } else {
     *      Вращение _против_ часовой стрелки;
     *  }
     *
     *  Для угла места:
     *  if (velocity > 0) {
     *      Вращение поднимающее антенну;
     *  } else {
     *      Вращение опускающее антенну;
     *  }
     *
     *  @attention В режиме позиционирования в точку при достижении указанной позиции двигатели фиксируют положение
     *  фиксированным моментом (задаваемым в настройках).
     *
     *  @param  azimuth_angle       Фиксированный угол по азимуту [импульсы энкодера]
     *  @param  azimuth_velocity    Скорость по азимуту [импульсы энкодера/с]
     *  @param  elevation_angle     Фиксированный угол места [импульсы энкодера]
     *  @param  elevation_angle     Скорость по углу места [импульсы энкодера/с]
     */
    void SetModeRun(int32_t azimuth_angle, int32_t azimuth_velocity, int32_t elevation_angle, int32_t elevation_velocity);

    /*! @brief Переключает систему управления в режим бездейсвтия.
     *
     *  @param  azimuth_flag        Флаг перевода в режим бездействия двигателя по азимуту
     *  @param  elevation_flag      Флаг перевода в режим бездействия двигателя по азимуту
     *
     *  @attention При возникновении ошибки этот вызов также сбрасывает состояние ошибки
     *             и приводит систему в состояние готовности к дальнейшей работе.
     */
    void SetModeIdle(bool azimuth_flag, bool elevation_flag);

    /*! @brief Получаем текущее состояние системы управления (динамически изменяемые)
     *
     *  @return Структуру Status, заполненную актуальными данными.
     */
    const std::atomic<SystemStatus>& GetStatus() const;

    /*! @brief Получаем статические параметры системы (не изменяющиеся с течением времени).
     *
     *  @return Структуру SystemInfo, заполненную актуальными данными.
     */
    const std::atomic<SystemInfo>& GetSystemInfo() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_pimpl;
};

} // namespaces
