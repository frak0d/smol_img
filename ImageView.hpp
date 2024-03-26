#pragma once
#include <QtCore>
#include <QtWidgets>

class ImageView : public QLabel
{
    QPixmap pixmap;
    QImage* input_img;
    
    std::function<void()> input_callback;
    std::function<void(int,int,int,int)> crop_callback;
    
    bool scale_to_fit;
    int crop_left, crop_right, crop_top, crop_bottom;
    
public:
    ImageView(QImage* img, bool scale2fit) : QLabel{"Drag & Drop Here"}, input_img{img}, scale_to_fit{scale2fit}
    {
        setAcceptDrops(true);
        setAlignment(Qt::AlignCenter);
        setSizePolicy({QSizePolicy::Ignored, QSizePolicy::Ignored});
    }
    
    void redraw_pixmap()
    {
        if (pixmap.isNull()) return;
        
        if (scale_to_fit)
            QLabel::setPixmap(pixmap.scaled(geometry().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            QLabel::setPixmap(pixmap);
    }
    
    bool scale2fit(){return scale_to_fit;};
    void scale2fit(bool b){scale_to_fit = b; redraw_pixmap();}
    
    void setPixmap(const QPixmap& _pixmap)
    {
        pixmap = _pixmap;
        redraw_pixmap();
    }
    
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        redraw_pixmap();
    }
    
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasImage() or event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }
    
    void dropEvent(QDropEvent *event) override
    {
        ;;;; if (event->mimeData()->hasImage())
            *input_img = event->mimeData()->imageData().value<QImage>();
        //
        else if (event->mimeData()->hasUrls())
            input_img->load(QUrl{event->mimeData()->urls().first().toString()}.toLocalFile());
        //
        else return;
        
        if (input_callback) input_callback(); // input_img has changed
    }
    
    void onImageInput(decltype(input_callback) callback)
    {
        input_callback = callback;
    }
    
    void onCropChange(decltype(crop_callback) callback)
    {
        crop_callback = callback;
    }
    
    void setCropMarks(int left, int right, int top, int bottom)
    {
        //
    }
};
