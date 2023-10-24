#include "TimerManager.h"

#include <QOpenGLContext>
#include <QDebug>

namespace gl_engine {

GeneralTimer::GeneralTimer(const std::string& name, const std::string& group, int queue_size, float average_weight)
    :m_name(name), m_group(group), m_queue_size(queue_size), m_average_weight(average_weight)
{
}

void GeneralTimer::start() {
    //assert(m_state == TimerStates::READY);
    _start();
    m_state = TimerStates::RUNNING;
}

void GeneralTimer::stop() {
    //assert(m_state == TimerStates::RUNNING);
    _stop();
    m_state = TimerStates::STOPPED;
}

bool GeneralTimer::fetch_result() {
    if (m_state == TimerStates::STOPPED) {
        float val = _fetch_result();
        this->m_last_measurement = val;
        m_state = TimerStates::READY;
        return true;
    }
    return false;
}

HostTimer::HostTimer(const std::string &name, const std::string& group, int queue_size, const float average_weight)
    :GeneralTimer(name, group, queue_size, average_weight)
{
}

void HostTimer::_start() {
    m_ticks[0] = std::chrono::high_resolution_clock::now();
}

void HostTimer::_stop() {
    m_ticks[1] = std::chrono::high_resolution_clock::now();
}

float HostTimer::_fetch_result() {
    std::chrono::duration<double> diff = m_ticks[1] - m_ticks[0];
    return ((float)(diff.count() * 1000.0));
}

#ifndef __EMSCRIPTEN__

GpuSyncQueryTimer::GpuSyncQueryTimer(const std::string &name, const std::string& group, int queue_size, const float average_weight)
    :GeneralTimer(name, group, queue_size, average_weight)
{
    for (int i = 0; i < 2; i++) {
        qTmr[i] = new QOpenGLTimerQuery(QOpenGLContext::currentContext());
        qTmr[i]->create();
    }
}

GpuSyncQueryTimer::~GpuSyncQueryTimer() {
    for (int i = 0; i < 2; i++) {
        delete qTmr[i];
    }
}

void GpuSyncQueryTimer::_start() {
    qTmr[0]->recordTimestamp();
}

void GpuSyncQueryTimer::_stop() {
    qTmr[1]->recordTimestamp();
}

float GpuSyncQueryTimer::_fetch_result() {
#ifdef QT_DEBUG
    if (!qTmr[0]->isResultAvailable() || !qTmr[1]->isResultAvailable()) {
        qWarning() << "A timer result is not available yet for timer" << m_name << ". The Thread will be blocked.";
    }
#endif
    GLuint64 elapsed_time = qTmr[1]->waitForResult() - qTmr[0]->waitForResult();
    return elapsed_time / 1000000.0f;
}


GpuAsyncQueryTimer::GpuAsyncQueryTimer(const std::string& name, const std::string& group, int queue_size, const float average_weight)
    :GeneralTimer(name, group, queue_size, average_weight)
{
    for (int i = 0; i < 4; i++) {
        m_qTmr[i] = new QOpenGLTimerQuery(QOpenGLContext::currentContext());
        m_qTmr[i]->create();
    }
    // Lets record a timestamp for the backbuffer such that we can fetch it and
    // don't have to implement special treatment for the first fetch
    m_qTmr[m_current_bb_offset]->recordTimestamp();
    m_qTmr[m_current_bb_offset + 1]->recordTimestamp();
}

GpuAsyncQueryTimer::~GpuAsyncQueryTimer() {
    for (int i = 0; i < 4; i++) {
        delete m_qTmr[i];
    }
}

void GpuAsyncQueryTimer::_start() {
    m_qTmr[m_current_fb_offset]->recordTimestamp();
}

void GpuAsyncQueryTimer::_stop() {
    m_qTmr[m_current_fb_offset + 1]->recordTimestamp();
}

float GpuAsyncQueryTimer::_fetch_result() {
#ifdef QT_DEBUG
    if (!m_qTmr[m_current_bb_offset]->isResultAvailable() || !m_qTmr[m_current_bb_offset + 1]->isResultAvailable()) {
        qWarning() << "A timer result is not available yet for timer" << m_name << ". The Thread will be blocked.";
    }
#endif
    GLuint64 elapsed_time = m_qTmr[m_current_bb_offset + 1]->waitForResult() - m_qTmr[m_current_bb_offset]->waitForResult();
    std::swap(m_current_fb_offset, m_current_bb_offset);
    return elapsed_time / 1000000.0f;
}

#endif

TimerManager::TimerManager()
{
}

void TimerManager::start_timer(const std::string &name)
{
    auto it = m_timer.find(name);
    if (it != m_timer.end()) m_timer[name]->start();
}

void TimerManager::stop_timer(const std::string &name)
{
    auto it = m_timer.find(name);
    if (it != m_timer.end()) m_timer[name]->stop();
}

QList<qTimerReport> TimerManager::fetch_results()
{
    QList<qTimerReport> new_values;
    for (const auto& tmr : m_timer_in_order) {
        if (tmr->fetch_result()) {
            new_values.push_back({ tmr->get_last_measurement(), tmr });
        }
    }
    return std::move(new_values);
}

std::shared_ptr<GeneralTimer> TimerManager::add_timer(const std::string &name, TimerTypes type, const std::string& group, int queue_size, float average_weight) {
    std::shared_ptr<GeneralTimer> tmr_base;
    switch(type) {

    case TimerTypes::CPU:
        tmr_base = static_pointer_cast<GeneralTimer>(std::make_shared<HostTimer>(name, group, queue_size, average_weight));
        break;
#ifndef __EMSCRIPTEN__
    case TimerTypes::GPU:
        tmr_base = static_pointer_cast<GeneralTimer>(std::make_shared<GpuSyncQueryTimer>(name, group, queue_size, average_weight));
        break;

    case TimerTypes::GPUAsync:
        tmr_base = static_pointer_cast<GeneralTimer>(std::make_shared<GpuAsyncQueryTimer>(name, group, queue_size, average_weight));
        break;
#endif
    default:
        qDebug() << "Timertype " << (int)type << " not supported on current target";
        return nullptr;
    }

    m_timer[name] = tmr_base;
    m_timer_in_order.push_back(tmr_base);
    return tmr_base;
}

} // namespace

