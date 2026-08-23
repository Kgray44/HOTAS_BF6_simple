#pragma once

#include <QStringList>

namespace hotas {

// Diagnostics are low-frequency UI events, but the history must still stay
// bounded across an unattended session. New entries retain natural log order.
class EventLog final {
public:
    explicit EventLog(int maximumEntries = 400)
        : m_maximumEntries(qMax(1, maximumEntries)) {}

    void append(const QString &entry)
    {
        m_entries.append(entry);
        while (m_entries.size() > m_maximumEntries) m_entries.removeFirst();
    }

    const QStringList &entries() const { return m_entries; }
    int maximumEntries() const { return m_maximumEntries; }

private:
    int m_maximumEntries;
    QStringList m_entries;
};

} // namespace hotas
