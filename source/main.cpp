#include <chrono>

using namespace std::literals;

#include <QtCore>
#include <QFileDialog>

#include "ui_main.h"
#include "ImageView.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setStyle("fusion");
    
    Ui::Main ui;
    QMainWindow window;
    ui.setupUi(&window);
    
    ui.img_format->addItems({"jpg","png"});
    
    QImage input_img;
    QBuffer encoded_img;
    QPixmap output_img;
    
    ImageView img_view(&input_img, true);
    QVBoxLayout img_layout(ui.image_frame);
    img_layout.addWidget(&img_view);
    
    int rotation = 0;
    
    img_view.onImageInput([&]
    {
        rotation = 0;
        ui.phony_btn->click();
    });
    
    img_view.onCropChange([&]
    (int left, int right, int top, int bottom)
    {
        ui.crop_left->setValue(left);
        ui.crop_right->setValue(right);
        ui.crop_top->setValue(top);
        ui.crop_bottom->setValue(bottom);
    });
    
    QObject::connect(ui.preview_original_sz, &QCheckBox::stateChanged, [&](int state)
    {
        img_view.scale2fit(state == Qt::Unchecked);
    });
    
    QObject::connect(ui.crop_in_percent, &QCheckBox::stateChanged, [&](int state)
    {
        if (input_img.isNull()) return;
        
        if (state == Qt::Checked)
        {
            // absolute to percentage
            ui.crop_top->setValue(ui.crop_top->value()*100.f / input_img.height());
            ui.crop_left->setValue(ui.crop_left->value()*100.f / input_img.width());
            ui.crop_right->setValue(ui.crop_right->value()*100.f / input_img.width());
            ui.crop_bottom->setValue(ui.crop_bottom->value()*100.f / input_img.height());
        }
        else
        {
            // percentage to absolute
            ui.crop_top->setValue(ui.crop_top->value()/100.f * input_img.height());
            ui.crop_left->setValue(ui.crop_left->value()/100.f * input_img.width());
            ui.crop_right->setValue(ui.crop_right->value()/100.f * input_img.width());
            ui.crop_bottom->setValue(ui.crop_bottom->value()/100.f * input_img.height());
        }
    });
    
    bool png_mode = false;
    
    auto compute_output = [&]
    {
        QImage img = input_img;
        int quality = png_mode ? 0 : ui.img_quality->sliderPosition(); // max compression for png
        
        if (ui.crop_in_percent->isChecked())
        {
            img = img.copy(QRect{QPoint(    ui.crop_left->value()/100.f  * img.width(),    ui.crop_top->value()   /100.f  * img.height()),
                                 QPoint((1-ui.crop_right->value()/100.f) * img.width(), (1-ui.crop_bottom->value()/100.f) * img.height())});
        }
        else // crop values in pixels
        {
            img = img.copy(QRect{QPoint{ui.crop_left->value(),                ui.crop_top->value()},
                                 QPoint{img.width() - ui.crop_right->value(), img.height() - ui.crop_bottom->value()}});            
        }
        
        // rotate
        img = img.transformed(QTransform{}.rotate(rotation));
        
        if (img.sizeInBytes() == 0)
        {
            img_view.setText("Error while Cropping the Image !"); // removes old pixmap
            ui.out_res_str->clear(); ui.out_size_str->clear();
            return;
        }
        
        QSize min_res = {std::min(img.width(), ui.min_width->value()),
                         std::min(img.height(), ui.min_height->value())};
        
        QSize res;
        
        QSize max_res = {std::min(img.width(), ui.max_width->value()),
                         std::min(img.height(), ui.max_height->value())};
        
        QSize _max_res = max_res;
        
        int max_size = ui.img_size->value() * 1024;
        bool quality_mode = false; // use user defined quality and adjust resolution first
        
        while (true) // binary search
        {
            res = (min_res + max_res) / 2;
            
            printf("(%d,%d) q=%d\n", res.width(), res.height(), quality); fflush(stdout);
            
            // scaling
            auto img2 = img.scaled(res, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            
            // encoding
            if (quality < 0 || quality > 100) break; // practical limit reached
            if (encoded_img.pos()) {encoded_img.buffer().clear(); encoded_img.seek(0);}
            img2.save(&encoded_img, ui.img_format->currentText().toStdString().c_str(), quality);
            
            const int delta = 0.05 * max_size;
            int diff = encoded_img.size() - max_size;
            
            if (quality_mode)
            {
                if (diff > 0) // too high
                {
                    quality -= 1;
                    continue; // skip resolution adj part
                }
                else break; // no need to handle "too low" since we always start from "too high" and decrement 1 at a time
                            // handling "too low" will lead to bistable condition causing deadlock on some images
            }
            
            if ((max_res.width() - min_res.width()) < 50 or
                (max_res.height() - min_res.height()) < 50)
            {
                if (diff > 0)
                {
                    if (!png_mode)
                    {                         // png_mode has no min_res, so can keep reducing
                        quality_mode = true; // but for jpeg we need to switch to quality reduction now
                        continue;
                    }
                }
                else break;
            }
            
            ;;;; if (diff > 0) // too high
                max_res = res;
            else if (diff < -delta) // too low
                min_res = res;
            else break; // perfect
        }
        
        // update preview
        output_img.loadFromData(encoded_img.buffer());
        img_view.setPixmap(output_img);
        
        // update stats
        ui.out_res_str->setText(QString{"%1x%2 q=%3"}.arg(output_img.width()).arg(output_img.height()).arg(quality));
        ui.out_size_str->setText(QString{"%1, %2 KB"}.arg(png_mode?"png":"jpg").arg(encoded_img.size()/1024.f, 0, 'f', 2));
    };
    
    QTimer compute_timer;
    bool update_pending = false;
    auto last_request_time = std::chrono::high_resolution_clock::now();
    
    auto request_update = [&]
    {
        last_request_time = std::chrono::high_resolution_clock::now();
        update_pending = true;
        
        ui.out_res_str->setText("Computing...");
        ui.out_size_str->clear();
    };
    
    QObject::connect(&compute_timer, &QTimer::timeout, [&]
    {
        if (update_pending)
        {
            auto time_now = std::chrono::high_resolution_clock::now();
            
            if (time_now - last_request_time >= 1s)
            {
                compute_output();
                update_pending = false;
            }
        }
    });
    compute_timer.start(100);
    
    int old_min_width, old_min_height, old_img_quality;
    
    QObject::connect(ui.img_format, &QComboBox::currentTextChanged, [&](const QString& format)
    {
        ;;;; if (format == "jpg")
        {
            if (png_mode)
            {
                ui.min_width->setValue(old_min_width);
                ui.min_height->setValue(old_min_height);
                ui.img_quality->setSliderPosition(old_img_quality);
                
                ui.min_width->setEnabled(true);
                ui.min_height->setEnabled(true);
                ui.img_quality->setEnabled(true);
                
                ui.min_width->blockSignals(false);
                ui.max_height->blockSignals(false);
                ui.img_quality->blockSignals(false);
                
                png_mode = false;
            }
        }
        else if (format == "png")
        {
            if (!png_mode)
            {
                ui.min_width->blockSignals(true);
                ui.max_height->blockSignals(true);
                ui.img_quality->blockSignals(true);
                
                old_min_width = ui.min_width->value();
                old_min_height = ui.min_height->value();
                old_img_quality = ui.img_quality->sliderPosition();
                
                ui.min_width->setValue(0);
                ui.min_height->setValue(0);
                ui.img_quality->setSliderPosition(0);
                
                ui.min_width->setEnabled(false);
                ui.min_height->setEnabled(false);
                ui.img_quality->setEnabled(false);
                
                png_mode = true;
            }
        }
    });
    
    QObject::connect(ui.phony_btn, &QPushButton::clicked, [&]
    {
        img_view.setCropMarks(ui.crop_left->value(),
                              ui.crop_right->value(),
                              ui.crop_top->value(),
                              ui.crop_bottom->value());
        
        if (!input_img.isNull()) request_update();
    });
    
    QObject::connect(ui.rotl_btn, &QPushButton::clicked, [&]
    {
        rotation -= 90;
        
        int crop_top = ui.crop_top->value();
        int crop_left = ui.crop_left->value();
        int crop_right = ui.crop_right->value();
        int crop_bottom = ui.crop_bottom->value();
        
        ui.crop_left->setValue(crop_top);
        ui.crop_bottom->setValue(crop_left);
        ui.crop_right->setValue(crop_bottom);
        ui.crop_top->setValue(crop_right);
        
        auto top = ui.crop_top;
        ui.crop_top = ui.crop_left;
        ui.crop_left = ui.crop_bottom;
        ui.crop_bottom = ui.crop_right;
        ui.crop_right = top;
        
        ui.phony_btn->click();
    });
    
    QObject::connect(ui.rotr_btn, &QPushButton::clicked, [&]
    {
        rotation += 90;
        
        int crop_top = ui.crop_top->value();
        int crop_left = ui.crop_left->value();
        int crop_right = ui.crop_right->value();
        int crop_bottom = ui.crop_bottom->value();
        
        ui.crop_right->setValue(crop_top);
        ui.crop_bottom->setValue(crop_right);
        ui.crop_left->setValue(crop_bottom);
        ui.crop_top->setValue(crop_left);
        
        auto top = ui.crop_top;
        ui.crop_top = ui.crop_right;
        ui.crop_right = ui.crop_bottom;
        ui.crop_bottom = ui.crop_left;
        ui.crop_left = top;
        
        ui.phony_btn->click();
    });
    
    QObject::connect(ui.copy_btn, &QPushButton::clicked, [&]
    {
        QApplication::clipboard()->setPixmap(output_img);
    });
    
    QObject::connect(ui.save_btn, &QPushButton::clicked, [&]
    {
        auto ext = '.' + ui.img_format->currentText();
        auto name = QFileDialog::getSaveFileName(nullptr, {}, {}, QString{"Image Files (*%1)"}.arg(ext));
        
        if (!name.isNull())
        {
            if (!name.endsWith(ext))
                name.append(ext);
            
            QFile file(name);
            
            if (file.open(QIODevice::WriteOnly))
                file.write(encoded_img.buffer()); 
        }
    });
    
    window.show();
    return app.exec();
}
