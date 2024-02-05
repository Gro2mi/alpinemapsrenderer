#include "GPX.h"

#include <srs.h>

#include <iostream>
#include <glm/gtx/io.hpp>

#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>

namespace nucleus {
namespace gpx {

    TrackPoint parse_trackpoint(QXmlStreamReader& xmlReader)
    {
        TrackPoint point;

        double latitude = xmlReader.attributes().value("lat").toDouble();
        double longitude = xmlReader.attributes().value("lon").toDouble();

        point.x = latitude;
        point.y = longitude;
        point.z = 0;

#if 1
        while (!xmlReader.atEnd() && !xmlReader.hasError()) {
            QXmlStreamReader::TokenType token = xmlReader.readNext();

            if (token == QXmlStreamReader::StartElement) {
                if (xmlReader.name() == QString("ele")) {
                    float elevation = xmlReader.readElementText().toDouble();
                    point.z = elevation;
                }

                break;
            }
        }
#endif

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

std::vector<glm::vec3> to_world_points(const gpx::Gpx& gpx)
{
    std::vector<glm::dvec3> track;

    for (const gpx::TrackSegment& segment : gpx.track) {
        track.insert(track.end(), segment.begin(), segment.end());
    }

    std::vector<glm::vec3> points(track.size());

    for (auto i = 0U; i < track.size(); i++) {
        points[i] = glm::vec3(srs::lat_long_alt_to_world(track[i]));
    }

    std::cout << points[0] << std::endl;
    std::cout << points[1] << std::endl;
    return points;
}

std::vector<glm::vec3> to_world_ribbon(const std::vector<glm::vec3>& points, float width)
{
    std::vector<glm::vec3> ribbon;

    const glm::vec3 offset = glm::vec3(0.0f, 0.0f, width);

    for (auto i = 0U; i < points.size() - 1U; i++)
    {
        auto a = points[i];

        // triangle 2
        ribbon.insert(ribbon.end(), {
            a - offset,
            a + offset,
        });
    }
    return ribbon;
}

std::vector<glm::vec3> to_triangle_ribbon(const std::vector<glm::vec3>& points, float width)
{
    std::vector<glm::vec3> ribbon;

    const glm::vec3 offset = glm::vec3(0.0f, 0.0f, width);

    for (std::size_t i = 0; i < points.size() - 1U; ++i)
    {
        glm::vec3 a = points[i];
        glm::vec3 b = points[i + 1];

        ribbon.insert(ribbon.end(), {
            a - offset, a + offset, b - offset, // triangle 1
            b - offset, b + offset, a + offset, // triangle 2
        });

    }
    return ribbon;
}

std::vector<glm::vec3> to_world_ribbon_with_normals(const std::vector<glm::vec3>& points, float width)
{
    std::vector<glm::vec3> ribbon;

    const glm::vec3 offset = glm::vec3(0.0f, 0.0f, width);

    for (size_t i = 0; i < points.size() - 1U; i++)
    {
        auto a = points[i];
        auto b = points[i + 1];

        // tangent is negative for vertices below the original line
        glm::vec3 tangent = glm::normalize(b - a);

        ribbon.insert(ribbon.end(), {
            a - offset, -tangent, b,
            a + offset,  tangent, b,
        });
    }
    return ribbon;


}

// 1 dimensional gaussian
float gaussian_1D(float x, float sigma = 1.0f)
{
    return (1.0 / std::sqrt(2 * M_PI * sigma)) * std::exp(-(x * x) / (2 * (sigma * sigma)));
}

void apply_gaussian_filter(std::vector<glm::vec3>& points, float sigma)
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
            value += points[i + j] * kernel[j + radius];

        points[i] = value;
    }
}

} // namespace nucleus
