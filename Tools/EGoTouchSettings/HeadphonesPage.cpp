#include "pch.h"
#include "MainWindow.xaml.h"
#include "HeadphoneImageLoader.h"
#include "HeadphoneConnectionPolicy.h"
#include "HeadphoneOptionIcons.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <cmath>

namespace winrt::EGoTouchSettings::implementation {
using namespace HuaweiHeadphones;

void MainWindow::InitializeHeadphones(){
    m_headphoneTimer=DispatcherTimer();m_headphoneTimer.Interval(std::chrono::seconds(30));
    m_headphoneTimer.Tick([this](auto const&,auto const&){RefreshHeadphonesAsync(false);});
    HeadphonePresetSlot().SelectedIndex(0);
    HeadphonePresetSlot().SelectionChanged([this](auto const&,auto const&){
        if(m_headphoneEqDialogOpen&&!m_headphoneRendering)m_headphoneEqDirty=true;
    });
    HeadphonePresetName().TextChanged([this](auto const&,auto const&){
        if(m_headphoneEqDialogOpen&&!m_headphoneRendering)m_headphoneEqDirty=true;
    });
    for(int i=0;i<10;++i){
        Grid row;row.ColumnSpacing(12);
        ColumnDefinition labelCol;labelCol.Width(GridLength{72});row.ColumnDefinitions().Append(labelCol);
        ColumnDefinition sliderCol;sliderCol.Width(GridLength{1,GridUnitType::Star});row.ColumnDefinitions().Append(sliderCol);
        ColumnDefinition valueCol;valueCol.Width(GridLength{64});row.ColumnDefinitions().Append(valueCol);
        TextBlock label;label.Text(L"频段 "+to_hstring(i+1));label.VerticalAlignment(VerticalAlignment::Center);row.Children().Append(label);
        Slider slider;slider.Minimum(-6);slider.Maximum(6);slider.StepFrequency(0.1);slider.SmallChange(0.1);slider.LargeChange(1);
        Automation::AutomationProperties::SetName(slider,L"频段 "+to_hstring(i+1)+L" 增益，单位分贝");
        slider.Value(0);slider.MinWidth(100);Controls::Grid::SetColumn(slider,1);row.Children().Append(slider);
        TextBlock value;value.Text(L"0.0 dB");value.HorizontalAlignment(HorizontalAlignment::Right);value.VerticalAlignment(VerticalAlignment::Center);
        Controls::Grid::SetColumn(value,2);row.Children().Append(value);
        slider.ValueChanged([this,value](auto const&,auto const& args){if(!m_headphoneRendering)m_headphoneEqDirty=true;wchar_t text[32];swprintf_s(text,L"%.1f dB",args.NewValue());value.Text(text);});
        m_headphoneBands[i]=slider;HeadphoneEqualizerBands().Children().Append(row);
    }
}
void MainWindow::UpdateHeadphoneRefreshPolicy(){
    if(!m_headphoneTimer)return;
    bool visible=m_mainUiVisible&&!m_closed&&DevicesPage().Visibility()==Visibility::Visible;
    bool was=m_headphonePageVisible;m_headphonePageVisible=visible;
    if(was!=visible)++m_headphoneUiEpoch;
    if(visible){if(!m_headphoneTimer.IsEnabled())m_headphoneTimer.Start();if(!was)RefreshHeadphonesAsync(false);}
    else{
        m_headphoneTimer.Stop();
        if(m_headphoneEqDialogOpen)try{HeadphoneEqualizerDialog().Hide();}catch(...){}
        if(was){m_headphoneClient->Cancel();ReleaseHeadphoneConnectionAsync();}
    }
}
fire_and_forget MainWindow::ReleaseHeadphoneConnectionAsync(){
    auto client=m_headphoneClient;co_await resume_background();
    try{client->ReleaseCancelled();}catch(...){}
}
void MainWindow::HeadphoneRefreshClicked(IInspectable const&,RoutedEventArgs const&){RefreshHeadphonesAsync(true);}
void MainWindow::HeadphoneFastReconnectToggled(IInspectable const&,RoutedEventArgs const&){
    if(m_updatingControls||!m_uiReady)return;
    const bool enabled=HeadphoneFastReconnectToggle().IsOn();
    if(!SendTrayCommand(EGoTouchTrayIpc::Command::SetHeadphoneFastReconnect,enabled?1:0)){
        m_updatingControls=true;HeadphoneFastReconnectToggle().IsOn(!enabled);m_updatingControls=false;
        ShowError(L"无法修改耳机重连设置",L"托盘没有响应，设置没有生效。");
    }
}
fire_and_forget MainWindow::HeadphoneReconnectClicked(IInspectable const&,RoutedEventArgs const&){
    if(m_headphoneBusy||m_headphonePopupReading||m_headphoneDialogPending||m_closed||!m_headphonePageVisible)co_return;
    const auto address=m_headphoneAddress;
    const auto selected=std::find_if(m_headphoneDevices.begin(),m_headphoneDevices.end(),
        [address](const auto& device){return device.address==address;});
    if(selected==m_headphoneDevices.end()||selected->connected||
       !HeadphoneConnections::IsHuaweiHeadphoneName(selected->name))co_return;
    if(!SendTrayCommand(EGoTouchTrayIpc::Command::ReconnectHeadphone,static_cast<LPARAM>(address))){
        ShowError(L"无法请求耳机重连",L"托盘没有响应，请稍后重试。");co_return;
    }
    auto lifetime=get_strong();const auto epoch=m_headphoneUiEpoch;
    SetHeadphoneBusy(true);HeadphoneStateText().Text(L"已请求 Windows 尝试重连…");
    auto ui=apartment_context();bool connected=false;
    // Poll cached state only while this explicit user action is pending.
    // Queueing a driver request is never treated as a successful connection.
    for(int attempt=0;attempt<16;++attempt){
        co_await std::chrono::milliseconds(500);
        co_await resume_background();
        try{connected=Client::ConnectedDevice(address).has_value();}catch(...){}
        try{co_await ui;}catch(...){co_return;}
        if(connected||m_closed||!m_headphonePageVisible||epoch!=m_headphoneUiEpoch)break;
    }
    if(m_closed)co_return;
    SetHeadphoneBusy(false);
    if(epoch!=m_headphoneUiEpoch){
        if(m_headphonePageVisible)RefreshHeadphonesAsync(false);
        co_return;
    }
    if(epoch==m_headphoneUiEpoch&&m_headphonePageVisible){
        if(connected)RefreshHeadphonesAsync(false);
        else HeadphoneStateText().Text(L"尚未检测到连接，请确认耳机已开机并可连接；也可使用 Windows 蓝牙设置。");
    }
}
void MainWindow::HeadphoneSelectionChanged(IInspectable const&,SelectionChangedEventArgs const&){
    if(m_headphoneRendering||m_headphoneBusy||m_headphonePopupReading||m_headphoneDialogPending||m_closed)return;
    int index=HeadphoneDevicePicker().SelectedIndex();
    if(index<0||size_t(index)>=m_headphoneDevices.size())return;
    ++m_headphoneUiEpoch;
    m_headphoneSelectionExplicit=true;
    m_headphoneAddress=m_headphoneDevices[index].address;
    m_headphoneSnapshot={};RenderHeadphones();RefreshHeadphonesAsync(false);
}
void MainWindow::SetHeadphoneBusy(bool value){
    m_headphoneBusy=value;const bool blocked=value||m_headphonePopupReading;
    const bool pageInteractive=!blocked&&!m_headphoneDialogPending;
    HeadphoneRefreshButton().IsEnabled(pageInteractive);HeadphoneDevicePicker().IsEnabled(pageInteractive);
    const auto selected=std::find_if(m_headphoneDevices.begin(),m_headphoneDevices.end(),
        [this](const auto& device){return device.address==m_headphoneAddress;});
    HeadphoneReconnectButton().IsEnabled(pageInteractive&&selected!=m_headphoneDevices.end()&&!selected->connected&&
        HeadphoneConnections::IsHuaweiHeadphoneName(selected->name));
    for(auto const& [id,combo]:m_headphoneCombos)combo.IsEnabled(pageInteractive&&m_headphoneSnapshot.ready);
    HeadphoneEqualizerEditor().IsEnabled(pageInteractive&&SupportsCustomEqualizer(m_headphoneSnapshot.model));
    HeadphonePeerRows().IsHitTestVisible(pageInteractive&&m_headphoneSnapshot.ready);
    if(m_headphoneEqDialogOpen){
        HeadphoneEqualizerFields().IsEnabled(!blocked&&!m_headphoneEqSaving);
        HeadphoneEqualizerDialog().IsPrimaryButtonEnabled(!blocked&&!m_headphoneEqSaving&&m_headphoneSnapshot.ready);
    }
    if(value)HeadphoneStateText().Text(L"正在读取耳机状态…");
    else if(!m_headphonePopupReading)DispatchPendingHeadphonePopup();
}
fire_and_forget MainWindow::RefreshHeadphonesAsync(bool full){
    if(m_headphonePopupReading){m_headphoneRefreshDeferred=true;co_return;}
    if(!m_uiReady||m_headphoneBusy||m_headphoneDialogPending||m_closed||!m_headphonePageVisible)co_return;
    auto lifetime=get_strong();auto ui=apartment_context();auto client=m_headphoneClient;
    auto address=m_headphoneAddress;auto epoch=m_headphoneUiEpoch;bool explicitSelection=m_headphoneSelectionExplicit;SetHeadphoneBusy(true);
    std::vector<Device> devices;Snapshot state;
    co_await resume_background();
    try{
        devices=Client::Devices();
        auto chosen=std::find_if(devices.begin(),devices.end(),[address](auto const& d){return d.address==address;});
        if(chosen==devices.end()||(!explicitSelection&&!chosen->connected)){
            auto connected=std::find_if(devices.begin(),devices.end(),[](auto const& d){return d.connected&&ModelFromName(d.name)!=Model::Unknown;});
            if(connected!=devices.end())chosen=connected;
        }
        if(chosen==devices.end())chosen=std::find_if(devices.begin(),devices.end(),[](auto const& d){return ModelFromName(d.name)!=Model::Unknown;});
        if(chosen==devices.end()&&!devices.empty())chosen=devices.begin();
        if(chosen!=devices.end()){
            state.name=chosen->name;state.address=chosen->address;state.model=ModelFromName(chosen->name);
            if(chosen->connected||full)state=client->Refresh(*chosen,full);
            else{client->Cancel();state.message=L"耳机未连接，请先在 Windows 蓝牙设置中连接。";}
        }else client->Cancel();
    }catch(...){state.failed=true;state.message=L"读取耳机失败，请稍后刷新重试。";}
    try{co_await ui;}catch(...){co_return;}
    if(m_closed)co_return;
    if(epoch!=m_headphoneUiEpoch){SetHeadphoneBusy(false);if(m_headphonePageVisible)RefreshHeadphonesAsync(false);co_return;}
    m_headphoneRendering=true;m_headphoneDevices=std::move(devices);HeadphoneDevicePicker().Items().Clear();
    int selected=-1;
    for(size_t i=0;i<m_headphoneDevices.size();++i){
        auto const& d=m_headphoneDevices[i];HeadphoneDevicePicker().Items().Append(box_value(d.name+(d.connected?L"":L"（未连接）")));
        if(d.address==state.address)selected=int(i);
    }
    HeadphoneDevicePicker().SelectedIndex(selected);m_headphoneAddress=state.address;
    m_headphoneRendering=false;m_headphoneSnapshot=std::move(state);SetHeadphoneBusy(false);RenderHeadphones();
}
void MainWindow::RenderHeadphones(){
    m_headphoneRendering=true;
    auto const& state=m_headphoneSnapshot;
    HeadphoneNameText().Text(state.name.empty()?L"华为耳机":hstring(state.name));
    HeadphoneStateText().Text(state.message);
    HeadphoneDevicePicker().PlaceholderText(m_headphoneDevices.empty()?L"未找到已配对的耳机":L"选择耳机");
    auto setBattery=[](auto const& indicator,auto const& battery){indicator.SetState(battery.level.has_value(),battery.level.value_or(0),battery.charging);};
    setBattery(HeadphoneLeftBattery(),state.battery[0]);setBattery(HeadphoneRightBattery(),state.battery[1]);setBattery(HeadphoneCaseBattery(),state.battery[2]);
    HeadphoneBatteryRow().Visibility(state.ready?Visibility::Visible:Visibility::Collapsed);
    bool rebuild=!m_headphoneRowsBuilt||m_headphoneRenderedModel!=state.model;
    auto controls=ControlProfile(state.model);
    std::stable_sort(controls.begin(),controls.end(),[](auto const& a,auto const& b){
        auto rank=[](auto const& group){return group==L"声音"?0:group==L"使用设置"?1:2;};return rank(a.group)<rank(b.group);
    });
    if(rebuild||m_headphoneEqAddress!=state.address){m_headphoneEqDirty=false;m_headphoneEqAddress=state.address;}
    if(rebuild){
        HeadphoneControlRows().Children().Clear();m_headphoneCombos.clear();
        m_headphoneMenuGroups.clear();m_headphoneLayoutDirty=true;
        auto resources=RootLayout().Resources();
        std::wstring group;Grid groupRows{nullptr};
        for(auto const& c:controls){
            if(c.group!=group){
                group=c.group;StackPanel header;header.Orientation(Orientation::Horizontal);header.Spacing(8);
                FontIcon symbol;symbol.Glyph(HeadphoneOptionIcons::GroupGlyph(group));
                symbol.Style(resources.Lookup(box_value(L"HeadphoneGroupIconStyle")).as<Style>());
                TextBlock title;title.Text(group);title.Style(resources.Lookup(box_value(L"SettingsGroupHeaderStyle")).as<Style>());
                title.Margin(Thickness{0});
                title.VerticalAlignment(VerticalAlignment::Center);
                header.Children().Append(symbol);header.Children().Append(title);
                Expander menu;menu.Style(resources.Lookup(box_value(L"HeadphoneMenuExpanderStyle")).as<Style>());
                menu.Header(header);Automation::AutomationProperties::SetName(menu,group);
                const auto expanded=m_headphoneExpandedGroups.find(group);
                menu.IsExpanded(expanded==m_headphoneExpandedGroups.end()||expanded->second);
                menu.RegisterPropertyChangedCallback(Expander::IsExpandedProperty(),
                    [weak=get_weak(),name=group](DependencyObject const& sender,DependencyProperty const&){
                        if(auto self=weak.get())self->m_headphoneExpandedGroups[name]=sender.as<Expander>().IsExpanded();
                    });
                groupRows=Grid();ColumnDefinition first;first.Width(GridLength{1,GridUnitType::Star});
                ColumnDefinition second;second.Width(GridLength{0,GridUnitType::Pixel});
                groupRows.ColumnDefinitions().Append(first);groupRows.ColumnDefinitions().Append(second);
                menu.Content(groupRows);HeadphoneControlRows().Children().Append(menu);
                m_headphoneMenuGroups.emplace_back(menu,groupRows);
            }
            Border item;item.Style(resources.Lookup(box_value(L"HeadphoneMenuRowStyle")).as<Style>());
            Grid row;row.ColumnSpacing(16);
            ColumnDefinition iconCol;iconCol.Width(GridLength{1,GridUnitType::Auto});row.ColumnDefinitions().Append(iconCol);
            ColumnDefinition textCol;textCol.Width(GridLength{1,GridUnitType::Star});row.ColumnDefinitions().Append(textCol);
            ColumnDefinition choiceCol;choiceCol.Width(GridLength{1,GridUnitType::Auto});row.ColumnDefinitions().Append(choiceCol);
            FontIcon icon;icon.Glyph(HeadphoneOptionIcons::Glyph(c.id));icon.Style(resources.Lookup(box_value(L"HeadphoneRowIconStyle")).as<Style>());row.Children().Append(icon);
            TextBlock label;label.Text(c.label);label.TextWrapping(TextWrapping::Wrap);label.VerticalAlignment(VerticalAlignment::Center);
            Controls::Grid::SetColumn(label,1);row.Children().Append(label);
            ComboBox combo;combo.MinWidth(180);combo.MaxWidth(230);combo.PlaceholderText(L"未读取到状态");
            combo.VerticalAlignment(VerticalAlignment::Center);Controls::Grid::SetColumn(combo,2);
            for(auto const& choice:c.choices){ComboBoxItem item;item.Content(box_value(choice.label));item.Tag(box_value(choice.value));combo.Items().Append(item);}
            combo.SelectionChanged([weak=get_weak(),id=c.id](auto const& sender,auto const&){
                if(auto self=weak.get()){
                    if(self->m_headphoneRendering||self->m_headphoneBusy||self->m_closed)return;
                    auto selected=sender.template as<ComboBox>().SelectedItem().template try_as<ComboBoxItem>();
                    if(selected)self->SetHeadphoneControlAsync(id,unbox_value<int>(selected.Tag()));
                }
            });
            row.Children().Append(combo);item.Child(row);groupRows.Children().Append(item);
            m_headphoneCombos.emplace(c.id,combo);
        }
        m_headphoneRowsBuilt=true;m_headphoneRenderedModel=state.model;
    }
    for(auto const& c:controls){auto combo=m_headphoneCombos.at(c.id);int index=-1;auto found=state.values.find(c.id);
        if(found!=state.values.end())for(size_t i=0;i<c.choices.size();++i)if(c.choices[i].value==found->second)index=int(i);
        if(c.id=="equalizer"&&found!=state.values.end()&&found->second>=100&&found->second<=102)
            combo.PlaceholderText(L"自定义音效 "+to_hstring(found->second-99));
        else combo.PlaceholderText(L"未读取到状态");
        combo.SelectedIndex(index);}
    HeadphoneControlRows().Visibility(state.model==Model::Unknown?Visibility::Collapsed:Visibility::Visible);
    for(auto const& [id,combo]:m_headphoneCombos)combo.IsEnabled(state.ready&&!m_headphoneBusy&&!m_headphonePopupReading&&!m_headphoneDialogPending);
    bool custom=SupportsCustomEqualizer(state.model);
    HeadphoneEqualizerCard().Visibility(custom?Visibility::Visible:Visibility::Collapsed);
    HeadphoneEqualizerEditor().IsEnabled(custom&&!m_headphoneBusy&&!m_headphonePopupReading&&!m_headphoneDialogPending);
    if(state.equalizer&&!m_headphoneEqDirty)for(size_t i=0;i<10;++i)m_headphoneBands[i].Value((*state.equalizer)[i]/10.0);
    HeadphonePeersCard().Visibility(state.model==Model::Buds7i?Visibility::Visible:Visibility::Collapsed);
    HeadphonePeerRows().Children().Clear();m_headphoneLayoutDirty=true;
    HeadphonePeersStatus().Text(state.peers.empty()?L"尚未读取到已配对设备，请点击“连接 / 刷新”。":L"移除后需重新配对才能再次连接该设备。");
    for(auto const& peer:state.peers){
        hstring name=L"已配对设备";try{if(!peer.name.empty())name=to_hstring(peer.name);}catch(...){}
        Grid row;row.ColumnSpacing(16);
        ColumnDefinition textCol;textCol.Width(GridLength{1,GridUnitType::Star});row.ColumnDefinitions().Append(textCol);
        ColumnDefinition buttonCol;buttonCol.Width(GridLength{1,GridUnitType::Auto});row.ColumnDefinitions().Append(buttonCol);
        TextBlock label;label.Text(name+(peer.connected?L"（已连接）":L""));label.VerticalAlignment(VerticalAlignment::Center);
        label.TextWrapping(TextWrapping::Wrap);row.Children().Append(label);
        Button remove;StackPanel removeContent;removeContent.Orientation(Orientation::Horizontal);removeContent.Spacing(8);
        FontIcon removeIcon;removeIcon.Glyph(HeadphoneOptionIcons::Glyph("remove_peer"));removeIcon.Style(RootLayout().Resources().Lookup(box_value(L"HeadphoneActionIconStyle")).as<Style>());
        TextBlock removeLabel;removeLabel.Text(L"移除");removeLabel.VerticalAlignment(VerticalAlignment::Center);removeContent.Children().Append(removeIcon);removeContent.Children().Append(removeLabel);
        remove.Content(removeContent);Automation::AutomationProperties::SetName(remove,L"移除");
        remove.IsEnabled(state.ready&&!m_headphoneBusy&&!m_headphonePopupReading&&!m_headphoneDialogPending);Controls::Grid::SetColumn(remove,1);
        remove.Click([weak=get_weak(),address=peer.address,name](auto const&,auto const&){if(auto self=weak.get())self->RemoveHeadphonePeerAsync(address,name);});
        row.Children().Append(remove);
        Border item;item.Style(RootLayout().Resources().Lookup(box_value(L"HeadphoneMenuRowStyle")).as<Style>());
        item.Child(row);HeadphonePeerRows().Children().Append(item);
    }
    ApplyHeadphoneCardLayout(m_deviceCardsWidth>=900.0);
    m_headphoneRendering=false;
    const auto artwork=HeadphoneArtworkSnapshot(state);
    LoadHeadphoneImageAsync(artwork);
    if(m_notificationWindow)get_self<implementation::NotificationWindow>(m_notificationWindow)->UpdateHeadphoneState(artwork);
}
void MainWindow::ApplyHeadphoneCardLayout(bool wide){
    if(!m_headphoneLayoutDirty&&m_headphoneCardsWide==wide)return;
    m_headphoneLayoutDirty=false;m_headphoneCardsWide=wide;
    const auto second=wide?GridLength{1,GridUnitType::Star}:GridLength{0,GridUnitType::Pixel};
    HeadphoneOverviewSecondColumn().Width(second);HeadphoneControlSecondColumn().Width(second);HeadphoneAdvancedSecondColumn().Width(second);
    const double gap=wide?24.0:0.0;
    HeadphoneOverviewCards().ColumnSpacing(gap);HeadphoneControlRows().ColumnSpacing(gap);HeadphoneAdvancedCards().ColumnSpacing(gap);
    // Menus share the information card's outer column; their items stay in one list.
    const auto layoutRows=[](Grid const& rows){
        rows.ColumnDefinitions().GetAt(1).Width(GridLength{0,GridUnitType::Pixel});rows.ColumnSpacing(0);
        const auto children=rows.Children();const auto size=children.Size();
        for(uint32_t i=0;i<size;++i){
            const auto item=children.GetAt(i).as<Border>();
            Controls::Grid::SetRow(item,static_cast<int>(i));Controls::Grid::SetColumn(item,0);
            item.BorderThickness(Thickness{0,i?1.0:0.0,0,0});
        }
        const uint32_t count=size;auto definitions=rows.RowDefinitions();
        while(definitions.Size()>count)definitions.RemoveAtEnd();
        while(definitions.Size()<count){RowDefinition definition;definition.Height(GridLength{1,GridUnitType::Auto});definitions.Append(definition);}
    };
    for(size_t i=0;i<m_headphoneMenuGroups.size();++i){
        const auto& [menu,rows]=m_headphoneMenuGroups[i];
        Controls::Grid::SetRow(menu,static_cast<int>(i));Controls::Grid::SetColumn(menu,0);Controls::Grid::SetColumnSpan(menu,1);
        layoutRows(rows);
    }
    const uint32_t count=static_cast<uint32_t>(m_headphoneMenuGroups.size());
    auto definitions=HeadphoneControlRows().RowDefinitions();
    while(definitions.Size()>count)definitions.RemoveAtEnd();
    while(definitions.Size()<count){RowDefinition definition;definition.Height(GridLength{1,GridUnitType::Auto});definitions.Append(definition);}
    layoutRows(HeadphonePeerRows());
    int advancedRow=0;
    Controls::Grid::SetRow(HeadphonePeersCard(),advancedRow);
    if(HeadphonePeersCard().Visibility()==Visibility::Visible)++advancedRow;
    Controls::Grid::SetColumn(HeadphoneEqualizerCard(),0);
    Controls::Grid::SetRow(HeadphoneEqualizerCard(),advancedRow);
    if(HeadphoneEqualizerCard().Visibility()==Visibility::Visible)++advancedRow;
    Controls::Grid::SetColumn(HeadphoneFastReconnectCard(),0);
    Controls::Grid::SetRow(HeadphoneFastReconnectCard(),advancedRow++);
    auto advancedDefinitions=HeadphoneAdvancedCards().RowDefinitions();
    while(advancedDefinitions.Size()>static_cast<uint32_t>(advancedRow))advancedDefinitions.RemoveAtEnd();
    while(advancedDefinitions.Size()<static_cast<uint32_t>(advancedRow)){RowDefinition definition;definition.Height(GridLength{1,GridUnitType::Auto});advancedDefinitions.Append(definition);}
}
fire_and_forget MainWindow::SetHeadphoneControlAsync(std::string id,int value){
    if(m_headphoneBusy||m_headphonePopupReading||m_headphoneDialogPending||!m_headphoneSnapshot.ready||m_closed)co_return;
    auto lifetime=get_strong();auto ui=apartment_context();auto client=m_headphoneClient;auto address=m_headphoneAddress;auto epoch=m_headphoneUiEpoch;
    SetHeadphoneBusy(true);Snapshot state;
    co_await resume_background();
    try{state=client->Set(address,id,value);}catch(...){state.failed=true;state.message=L"修改耳机设置失败，请刷新后重试。";}
    try{co_await ui;}catch(...){co_return;}if(m_closed)co_return;
    if(epoch!=m_headphoneUiEpoch){SetHeadphoneBusy(false);if(m_headphonePageVisible)RefreshHeadphonesAsync(false);co_return;}
    if(address==m_headphoneAddress)m_headphoneSnapshot=std::move(state);
    SetHeadphoneBusy(false);RenderHeadphones();
}
void MainWindow::HeadphoneEqualizerClicked(IInspectable const&,RoutedEventArgs const&){ShowHeadphoneEqualizerAsync();}
fire_and_forget MainWindow::ShowHeadphoneEqualizerAsync(){
    if(m_closed||!m_headphonePageVisible||m_headphoneBusy||m_headphonePopupReading||
       m_headphoneDialogPending||m_updateDialogOpen||m_exitDialogOpen||
       !SupportsCustomEqualizer(m_headphoneSnapshot.model)||!RootLayout().XamlRoot())co_return;
    auto lifetime=get_strong();
    m_headphoneEqDialogAddress=m_headphoneAddress;m_headphoneEqDialogEpoch=m_headphoneUiEpoch;
    m_headphoneDialogPending=true;m_headphoneEqDialogOpen=true;
    auto dialog=HeadphoneEqualizerDialog();
    try{
        HeadphoneEqualizerStatus().Text(L"");HeadphoneEqualizerStatus().Visibility(Visibility::Collapsed);
        if(!m_headphoneSnapshot.ready){
            HeadphoneEqualizerStatus().Text(L"可先编辑草稿；连接耳机并在主页刷新后可保存应用。");
            HeadphoneEqualizerStatus().Visibility(Visibility::Visible);
        }
        dialog.XamlRoot(RootLayout().XamlRoot());dialog.RequestedTheme(RootLayout().ActualTheme());
        HeadphoneEqualizerScroll().MaxHeight(std::clamp(RootLayout().ActualHeight()-220.0,120.0,480.0));
        SetHeadphoneBusy(m_headphoneBusy);
        co_await dialog.ShowAsync();
    }
    catch(...){if(!m_closed)HeadphoneStateText().Text(L"无法打开均衡器，请稍后重试。");}
    m_headphoneEqDialogOpen=false;m_headphoneDialogPending=false;
    if(!m_closed){
        SetHeadphoneBusy(m_headphoneBusy);DispatchPendingHeadphonePopup();
        if(m_headphonePageVisible&&m_mainUiVisible)HeadphoneEqualizerEditor().Focus(FocusState::Programmatic);
    }
}
void MainWindow::HeadphoneEqualizerDialogClosing(ContentDialog const&,ContentDialogClosingEventArgs const& args){
    if(m_headphoneEqSaving&&!m_closed&&m_headphonePageVisible&&m_mainUiVisible)args.Cancel(true);
}
fire_and_forget MainWindow::HeadphoneEqualizerPrimaryClicked(ContentDialog const&,ContentDialogButtonClickEventArgs const& args){
    auto lifetime=get_strong();auto eventArgs=args;
    eventArgs.Cancel(true);auto deferral=eventArgs.GetDeferral();
    if(m_headphoneEqSaving){try{deferral.Complete();}catch(...){}co_return;}
    m_headphoneEqSaving=true;
    try{
        HeadphoneEqualizerStatus().Text(L"正在保存并确认音效…");HeadphoneEqualizerStatus().Visibility(Visibility::Visible);
        HeadphoneEqualizerFields().IsEnabled(false);HeadphoneEqualizerDialog().IsPrimaryButtonEnabled(false);
        const bool saved=co_await SetHeadphoneEqualizerAsync();
        if(saved&&!m_closed&&m_headphoneEqDialogOpen)eventArgs.Cancel(false);
    }catch(...){
        if(!m_closed&&m_headphoneEqDialogOpen){
            HeadphoneEqualizerStatus().Text(L"保存音效失败，请关闭后刷新耳机再试。");
            HeadphoneEqualizerStatus().Visibility(Visibility::Visible);
        }
    }
    m_headphoneEqSaving=false;
    if(!m_closed&&m_headphoneEqDialogOpen)SetHeadphoneBusy(m_headphoneBusy);
    try{deferral.Complete();}catch(...){}
}
Windows::Foundation::IAsyncOperation<bool> MainWindow::SetHeadphoneEqualizerAsync(){
    if(m_headphoneBusy||m_headphonePopupReading||!m_headphoneSnapshot.ready||m_closed||
       !m_headphoneEqDialogOpen||m_headphoneEqDialogAddress!=m_headphoneAddress||m_headphoneEqDialogEpoch!=m_headphoneUiEpoch){
        if(!m_closed){HeadphoneEqualizerStatus().Text(L"耳机连接已变化，请关闭后刷新再试。");HeadphoneEqualizerStatus().Visibility(Visibility::Visible);}
        co_return false;
    }
    std::array<int,10> gains;for(size_t i=0;i<10;++i)gains[i]=int(std::lround(m_headphoneBands[i].Value()*10));
    int slot=100+std::max(0,HeadphonePresetSlot().SelectedIndex());std::string name=to_string(HeadphonePresetName().Text());
    if(name.empty()||name.size()>32){
        HeadphoneEqualizerStatus().Text(L"音效名称不能为空或过长，请缩短后重试。");HeadphoneEqualizerStatus().Visibility(Visibility::Visible);co_return false;
    }
    auto lifetime=get_strong();auto ui=apartment_context();auto client=m_headphoneClient;auto address=m_headphoneAddress;auto epoch=m_headphoneUiEpoch;
    SetHeadphoneBusy(true);Snapshot state;bool completed=false;
    co_await resume_background();
    try{state=client->SetEqualizer(address,slot,name,gains);completed=true;}catch(...){}
    try{co_await ui;}catch(...){co_return false;}
    if(m_closed){m_headphoneBusy=false;co_return false;}
    if(epoch!=m_headphoneUiEpoch||address!=m_headphoneAddress||!m_headphoneEqDialogOpen){
        SetHeadphoneBusy(false);if(m_headphonePageVisible&&!m_headphoneDialogPending)RefreshHeadphonesAsync(false);co_return false;
    }
    const bool saved=completed&&!state.failed&&state.address==address;
    if(saved){
        m_headphoneEqDirty=false;m_headphoneSnapshot=std::move(state);
        SetHeadphoneBusy(false);RenderHeadphones();
    }else{
        if(completed&&state.address==address&&!state.ready)m_headphoneSnapshot.ready=false;
        const hstring message=completed&&!state.message.empty()?hstring(state.message):hstring(L"保存音效失败，请关闭后刷新耳机再试。");
        HeadphoneEqualizerStatus().Text(message);HeadphoneEqualizerStatus().Visibility(Visibility::Visible);
        HeadphoneStateText().Text(message);SetHeadphoneBusy(false);
    }
    co_return saved;
}
fire_and_forget MainWindow::RemoveHeadphonePeerAsync(std::array<uint8_t,6> peer,hstring name){
    if(m_headphoneBusy||m_headphonePopupReading||m_headphoneDialogPending||m_updateDialogOpen||m_exitDialogOpen||!m_headphoneSnapshot.ready||m_closed)co_return;
    auto lifetime=get_strong();auto address=m_headphoneAddress;auto epoch=m_headphoneUiEpoch;
    ContentDialog dialog;dialog.XamlRoot(RootLayout().XamlRoot());dialog.RequestedTheme(RootLayout().ActualTheme());
    dialog.Title(box_value(L"移除配对设备？"));dialog.Content(box_value(L"将从耳机中移除“"+name+L"”的配对记录。若它是当前电脑，耳机连接可能会断开。"));
    dialog.PrimaryButtonText(L"移除");dialog.CloseButtonText(L"取消");dialog.DefaultButton(ContentDialogButton::Close);
    m_headphoneDialogPending=true; ContentDialogResult result;
    try{result=co_await dialog.ShowAsync();}catch(...){m_headphoneDialogPending=false;DispatchPendingHeadphonePopup();co_return;}
    m_headphoneDialogPending=false;
    DispatchPendingHeadphonePopup();
    if(result!=ContentDialogResult::Primary||m_closed||epoch!=m_headphoneUiEpoch||m_headphoneBusy||m_headphonePopupReading)co_return;
    auto ui=apartment_context();auto client=m_headphoneClient;SetHeadphoneBusy(true);Snapshot state;
    co_await resume_background();
    try{state=client->RemovePeer(address,peer);}catch(...){state.failed=true;state.message=L"移除配对记录失败，请刷新后重试。";}
    try{co_await ui;}catch(...){co_return;}if(m_closed)co_return;
    if(epoch!=m_headphoneUiEpoch){SetHeadphoneBusy(false);if(m_headphonePageVisible)RefreshHeadphonesAsync(false);co_return;}
    if(address==m_headphoneAddress)m_headphoneSnapshot=std::move(state);
    SetHeadphoneBusy(false);RenderHeadphones();
}
}
