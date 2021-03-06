// For conditions of distribution and use, see copyright notice in license.txt

#ifndef incl_UiModule_VoiceControllerWidget_h
#define incl_UiModule_VoiceControllerWidget_h

#include <QObject>
#include <QWidget>
#include "ui_VoiceControl.h"

namespace Communications
{
    namespace InWorldVoice
    {
        class SessionInterface;
    }
}

namespace CommUI
{
    class VoiceUserWidget;
    /**
     *
     *
     */
    class VoiceControllerWidget : public QWidget, private Ui::VoiceControl
    {
        Q_OBJECT
    public:
        VoiceControllerWidget(Communications::InWorldVoice::SessionInterface* voice_session);
        virtual ~VoiceControllerWidget();

    private slots:
        void ApplyMuteAllSelection();
        void UpdateUI();
        void UpdateParticipantList();

    private:
        Communications::InWorldVoice::SessionInterface* voice_session_;
        QGraphicsProxyWidget* voice_users_proxy_widget_;
        QList<VoiceUserWidget *> user_widgets_;
    };
} // namespace CommUI

#endif // incl_UiModule_VoiceControllerWidget_h
