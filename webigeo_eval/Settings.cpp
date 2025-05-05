/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2025 Patrick Komon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "Settings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace webigeo_eval {

void Settings::write_to_json_file(const Settings& settings, const std::filesystem::path& output_path)
{
    QJsonObject object;

    object["trajectory_resolution_multiplier"] = qint64(settings.trajectory_resolution_multiplier);
    object["num_steps"] = qint64(settings.num_steps);
    object["num_paths_per_release_cell"] = qint64(settings.num_paths_per_release_cell);
    object["random_contribution"] = settings.random_contribution;
    object["persistence_contribution"] = settings.persistence_contribution;
    object["alpha"] = settings.runout_flowpy_alpha;
    object["aabb_file_path"] = QString::fromStdString(settings.aabb_file_path);
    object["release_points_texture_path"] = QString::fromStdString(settings.release_points_texture_path);
    object["heightmap_texture_path"] = QString::fromStdString(settings.heightmap_texture_path);
    object["output_dir_path"] = QString::fromStdString(settings.output_dir_path);

    QJsonDocument doc;
    doc.setObject(object);

    QFile output_file(output_path);
    output_file.open(QIODevice::WriteOnly);
    output_file.write(doc.toJson());
    output_file.close();
}

Settings Settings::read_from_json_file(const std::filesystem::path& input_path)
{

    QJsonObject object;
    {
        QJsonDocument document;

        QFile input_file(input_path);
        input_file.open(QIODevice::ReadOnly);
        QByteArray data = input_file.readAll();
        document = QJsonDocument::fromJson(data);
        input_file.close();
        assert(document.isObject());
        object = document.object();
    }

    Settings settings;
    settings.trajectory_resolution_multiplier = uint32_t(object["trajectory_resolution_multiplier"].toInteger());
    settings.num_steps = uint32_t(object["num_steps"].toInteger());
    settings.num_paths_per_release_cell = uint32_t(object["num_paths_per_release_cell"].toInteger());
    settings.random_contribution = float(object["random_contribution"].toDouble());
    settings.persistence_contribution = float(object["persistence_contribution"].toDouble());
    settings.runout_flowpy_alpha = float(object["alpha"].toDouble());
    settings.aabb_file_path = object["aabb_file_path"].toString().toStdString();
    settings.release_points_texture_path = object["release_points_texture_path"].toString().toStdString();
    settings.heightmap_texture_path = object["heightmap_texture_path"].toString().toStdString();
    settings.output_dir_path = object["output_dir_path"].toString().toStdString();
    return settings;
}
} // namespace webigeo_eval
