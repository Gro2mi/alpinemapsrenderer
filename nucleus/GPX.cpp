#include "GPX.h"

#include <srs.h>

#include <iostream>
#include <glm/gtx/io.hpp>

#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>
#include <QDateTime>

namespace nucleus {
namespace gpx {

    TrackPoint parse_trackpoint(QXmlStreamReader& xmlReader)
    {
        TrackPoint point;

        double latitude = xmlReader.attributes().value("lat").toDouble();
        double longitude = xmlReader.attributes().value("lon").toDouble();

        point.latitude = latitude;
        point.longitude = longitude;
        point.elevation = 0;

        while (!xmlReader.atEnd() && !xmlReader.hasError()) {
            QXmlStreamReader::TokenType token = xmlReader.readNext();

            if (token == QXmlStreamReader::StartElement) {

                QStringView name = xmlReader.name();

                if (name == QString("ele")) {
                    float elevation = xmlReader.readElementText().toDouble();
                    point.elevation = elevation;
                } else if (name == QString("time")) {
                    QString iso_date_string = xmlReader.readElementText();
                    QDateTime date_time = QDateTime::fromString(iso_date_string, Qt::ISODate);

                    if (date_time.isValid()) {
                        point.timestamp = date_time;
                    } else {
                        qDebug() << "Failed to parse date " << iso_date_string;
                    }
                } else {
                    break;
                }
            }
        }

        return point;
    }

    std::unique_ptr<Gpx> parse(const QString& path)
    {
        std::unique_ptr<Gpx> gpx = std::make_unique<Gpx>();

        QFile file(path);

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "Could not open file " << path.toStdString() << std::endl;
            return nullptr;
        }

        QXmlStreamReader xmlReader(&file);

        while (!xmlReader.atEnd() && !xmlReader.hasError()) {
            QXmlStreamReader::TokenType token = xmlReader.readNext();

            if (token == QXmlStreamReader::StartElement) {
                // Handle the start element
                QStringView name = xmlReader.name();

                if (name == QString("trk")) {
                    // nothing to do here
                } else if (name == QString("trkpt")) {
                    if (gpx->track.size() == 0) {
                        gpx->track.push_back(TrackSegment());
                    }

                    TrackPoint track_point = parse_trackpoint(xmlReader);
                    gpx->track.back().push_back(track_point);

                } else if (name == QString("trkseg")) {
                    gpx->track.push_back(TrackSegment());

                } else if (name == QString("wpt")) {
                    std::cerr << "'wpt' NOT IMPLEMENTED!\n";
                    return nullptr;

                } else if (name == QString("rte")) {
                    std::cerr << "'rte' NOT IMPLEMENTED!\n";
                    return nullptr;
                }
            } else if (token == QXmlStreamReader::EndElement) {
                // Handle the end element
            } else if (token == QXmlStreamReader::Characters && !xmlReader.isWhitespace()) {
                // Handle text content
            }
        }

        if (xmlReader.hasError()) {
            // Handle the XML parsing error
            qDebug() << "XML Parsing Error: " << xmlReader.errorString();
            return nullptr;
        }

        file.close();
        return gpx;
    }
} // namespace gpx

std::vector<glm::vec4> to_world_points(const gpx::Gpx& gpx)
{
    std::vector<glm::dvec4> track;

    QDateTime epoch(QDate(1970, 1, 1).startOfDay());

    for (const gpx::TrackSegment& segment : gpx.track) {
        //for (const gpx::TrackPoint &point : segment) {
        for (size_t i = 0U; i < segment.size(); ++i) {

            double time_since_epoch = 0;

            if (i > 0) {
                time_since_epoch = static_cast<double>(segment[i - 1].timestamp.msecsTo(segment[i].timestamp));
            }

            track.push_back({segment[i].latitude, segment[i].longitude, segment[i].elevation, time_since_epoch});
        }
    }

    std::vector<glm::vec4> points(track.size());

    for (auto i = 0U; i < track.size(); i++) {
        points[i] = glm::vec4(srs::lat_long_alt_to_world(track[i]), track[i].w);
    }

    std::cout << "First Point: " << points[0] << std::endl;

    return points;
}

std::vector<glm::vec3> triangle_strip_ribbon(const std::vector<glm::vec3>& points, float width)
{
    std::vector<glm::vec3> ribbon;

    const glm::vec3 offset = glm::vec3(0.0f, 0.0f, width);

    const glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 down = glm::vec3(0.0f, 0.0f, -1.0f);

    for (auto i = 0U; i < points.size() - 1U; i++)
    {
        auto a = points[i];

        ribbon.insert(ribbon.end(), {
            a - offset, down,
            a + offset, up,
        });
    }
    return ribbon;
}

std::vector<glm::vec3> triangles_ribbon(const std::vector<glm::vec4>& points, float width, int index_offset)
{

    float max_delta_time = 0;
    float max_dist = 0;
    float max_speed = 0;

    for (size_t i = 0; i < points.size(); ++i) {
        glm::vec4 point = points[i];
        float delta_time = point.w;
        max_delta_time = glm::max(max_delta_time, delta_time);

        if (i > 0) {
            float dist = glm::distance(points[i], points[i - 1]);
            max_dist = glm::max(max_dist, dist);

            float speed = dist / delta_time;

            max_speed = glm::max(max_speed, speed);
        }
    }

    std::cout << "Max delta: " << max_delta_time << std::endl;
    std::cout << "Max Dist: " << max_dist << std::endl;
    std::cout << "max speed: " << max_speed << std::endl;

    std::vector<glm::vec3> ribbon;

    const glm::vec3 offset = glm::vec3(0.0f, 0.0f, width);

    for (size_t i = 0; i < points.size() - 1U; i++)
    {
        auto a = glm::vec3(points[i]);
        auto b = glm::vec3(points[i + 1]);
        auto d = glm::normalize(b - a);

        auto up = glm::vec3(0.0f, 0.0f, 1.0f);
        auto down = glm::vec3(0.0f, 0.0f, -1.0f);

        auto start = glm::vec3(1.0f, 0.0f, 0.0f);
        auto end = glm::vec3(-1.0f, 0.0f, 0.0f);

        auto index = glm::vec3(0.0f, static_cast<float>(index_offset + i), 0.0f);

        float delta_time = points[i + 1].w;

        float speed = glm::distance(a, b) / delta_time;
        float vertical_speed = glm::abs(a.z - b.z) / delta_time;

        //std::cout << "speed: " << speed << ", max_speed: " << max_speed << std::endl;

        auto metadata = glm::vec3(
            speed,
            vertical_speed,
            0.0f
        );

        // triangle 1
        ribbon.insert(ribbon.end(), {
            a + offset, d, up + start + index, metadata,
            a - offset, d, down + start + index, metadata,
            b - offset, d, down + end + index, metadata,
        });

        // triangle 2
        ribbon.insert(ribbon.end(), {
            a + offset, d, up + start + index, metadata,
            b - offset, d, down + end + index, metadata,
            b + offset, d, up + end + index, metadata,
        });
    }
    return ribbon;
}

std::vector<unsigned> ribbon_indices(unsigned point_count)
{
    std::vector<unsigned> indices;

    for (unsigned i = 0; i < point_count; ++i)
    {
        unsigned idx = i * 2;
        // triangle 1
        indices.insert(indices.end(), { idx, idx + 1, idx + 3});

        // triangle 2
        indices.insert(indices.end(), { idx + 3, idx + 2, idx});
    }

    return indices;
}

// 1 dimensional gaussian
float gaussian_1D(float x, float sigma = 1.0f)
{
    return (1.0 / std::sqrt(2 * M_PI * sigma)) * std::exp(-(x * x) / (2 * (sigma * sigma)));
}

void apply_gaussian_filter(std::vector<glm::vec4>& points, float sigma)
{
    const int radius = 2;
    const int kernel_size = (radius * 2) + 1;
    float kernel[kernel_size];
    float kernel_sum = 0.0f;

    // create kernel
    for (int x = -radius; x <= radius; x++)
    {
        kernel[x + radius] = gaussian_1D(static_cast<float>(x), sigma);
        kernel_sum += kernel[x + radius];
    }

    // normalize kernel
    for (int i = 0; i < kernel_size; i++) kernel[i] /= kernel_sum;

    for (int i = radius; i < static_cast<int>(points.size()) - radius; i++)
    {
        glm::vec3 value(0.0f);

        for (int j = -radius; j <= radius; j++)
            value += glm::vec3(points[i + j]) * kernel[j + radius];

        points[i] = glm::vec4(value, points[i].w);
    }
}

void reduce_point_count(std::vector<glm::vec4>& points, float threshold)
{
    std::vector<glm::vec4> old_points = points;

    points.clear();

    for (size_t i = 0; i < old_points.size() - 1; ++i) {
        auto current_point = old_points[i];

        points.push_back(current_point);

        while (glm::distance(glm::vec3(current_point), glm::vec3(old_points[i + 1])) < threshold && i < old_points.size() - 1) {
            ++i;
        }
    }
}

} // namespace nucleus
