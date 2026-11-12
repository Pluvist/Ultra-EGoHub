#include "HuaweiHeadphonesProtocol.h"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <stdexcept>

namespace HuaweiHeadphones {
const Bytes* Frame::Find(uint8_t tag) const {
    auto p=std::find_if(fields.begin(),fields.end(),[tag](auto const& f){return f.tag==tag;});
    return p==fields.end()?nullptr:&p->value;
}
uint16_t Crc(std::span<const uint8_t> bytes) {
    uint16_t crc=0;
    for(auto v:bytes){crc^=uint16_t(v)<<8;for(int i=0;i<8;++i)crc=uint16_t((crc<<1)^((crc&0x8000)?0x1021:0));}
    return crc;
}
Bytes Encode(uint8_t service,uint8_t command,std::span<const Field> fields) {
    Bytes out{0x5a,0,0,0,service,command};
    for(auto const& f:fields){
        if(f.value.size()>255)throw std::invalid_argument("AAM field too large");
        out.push_back(f.tag);out.push_back(uint8_t(f.value.size()));out.insert(out.end(),f.value.begin(),f.value.end());
    }
    if(out.size()>4094)throw std::invalid_argument("AAM packet too large");
    const auto length=out.size()-3;out[1]=uint8_t(length>>8);out[2]=uint8_t(length);
    auto crc=Crc(out);out.push_back(uint8_t(crc>>8));out.push_back(uint8_t(crc));return out;
}
Bytes Packet(uint8_t s,uint8_t c,std::initializer_list<Field> fields){return Encode(s,c,fields);}
std::vector<Frame> Decoder::Feed(std::span<const uint8_t> bytes) {
    std::vector<Frame> out;
    // A broken peer cannot grow the stream buffer without bound. Ingest chunks
    // incrementally so a large read with many small frames is still lossless.
    for(auto v:bytes){
        m_bytes.push_back(v);
        for(;;){
            auto magic=std::find(m_bytes.begin(),m_bytes.end(),uint8_t{0x5a});
            m_bytes.erase(m_bytes.begin(),magic);
            if(m_bytes.size()<3)break;
            size_t size=(size_t(m_bytes[1])<<8)+m_bytes[2]+5;
            if(size<8||size>4096){m_bytes.erase(m_bytes.begin());continue;}
            if(m_bytes.size()<size)break;
            const auto expected=uint16_t((m_bytes[size-2]<<8)|m_bytes[size-1]);
            if(m_bytes[3]!=0||Crc(std::span(m_bytes).first(size-2))!=expected){m_bytes.erase(m_bytes.begin());continue;}
            Frame frame{m_bytes[4],m_bytes[5],{}};
            size_t pos=6;bool valid=true;
            while(pos<size-2){
                if(pos+2>size-2){valid=false;break;}
                uint8_t tag=m_bytes[pos++];size_t n=m_bytes[pos++];
                if(pos+n>size-2||frame.Find(tag)){valid=false;break;}
                frame.fields.push_back({tag,Bytes(m_bytes.begin()+pos,m_bytes.begin()+pos+n)});pos+=n;
            }
            if(valid)out.push_back(std::move(frame));
            m_bytes.erase(m_bytes.begin(),m_bytes.begin()+size);
        }
    }
    return out;
}
Model ModelFromResource(std::string_view id){
    static constexpr std::pair<std::string_view,Model> ids[]{
        {"000027",Model::Buds3},{"000129",Model::Buds4},{"000135",Model::Buds4E},{"000141",Model::Buds5},
        {"000145",Model::Buds5i},{"000169",Model::Se4Anc},{"000153",Model::Buds6i},
        {"000155",Model::Buds6},{"000149",Model::Pro3},{"00016D",Model::Pro5},
        {"000163",Model::Buds7i},{"000174",Model::Buds7},{"000167",Model::Clip2},
        {"000173",Model::Clip2},{"00015D",Model::Arc},{"000139",Model::Eyewear},{"00014F",Model::Eyewear2}};
    for(auto [s,m]:ids)if(s==id)return m;return Model::Unknown;
}
std::wstring_view ModelName(Model m){
    switch(m){
    case Model::Buds3:return L"HUAWEI FreeBuds 3";case Model::Buds4:return L"HUAWEI FreeBuds 4";
    case Model::Buds4E:return L"HUAWEI FreeBuds 4E";case Model::Buds5:return L"HUAWEI FreeBuds 5";
    case Model::Buds5i:return L"HUAWEI FreeBuds 5i";case Model::Se4Anc:return L"HUAWEI FreeBuds SE 4 ANC";
    case Model::Buds6i:return L"HUAWEI FreeBuds 6i";case Model::Buds6:return L"HUAWEI FreeBuds 6";
    case Model::Pro3:return L"HUAWEI FreeBuds Pro 3";case Model::Pro4:return L"HUAWEI FreeBuds Pro 4";
    case Model::Pro5:return L"HUAWEI FreeBuds Pro 5";case Model::Buds7i:return L"HUAWEI FreeBuds 7i";
    case Model::Buds7:return L"HUAWEI FreeBuds 7";case Model::Clip:return L"HUAWEI FreeClip";
    case Model::Clip2:return L"HUAWEI FreeClip 2";case Model::Arc:return L"HUAWEI FreeArc";
    case Model::Eyewear:return L"HUAWEI Eyewear";case Model::Eyewear2:return L"HUAWEI Eyewear 2";
    default:return L"耳机";}
}
Model ModelFromName(std::wstring_view name){
    auto normalize=[](std::wstring_view s){std::wstring v;for(auto c:s)if(iswalnum(c))v+=wchar_t(towlower(c));return v;};
    auto value=normalize(name);if(value.starts_with(L"huawei"))value.erase(0,6);
    if(value==L"freeclip2典藏版")return Model::Clip2;
    if(value==L"eyewear3")return Model::Eyewear;
    for(int i=int(Model::Buds3);i<=int(Model::Eyewear2);++i){
        auto m=Model(i);auto n=normalize(ModelName(m));if(n.starts_with(L"huawei"))n.erase(0,6);
        if(value==n)return m;
    }
    return Model::Unknown;
}
std::optional<Identity> ReadIdentity(const Frame& f){
    if(f.service!=1||f.command!=7)return {};
    auto model=f.Find(0x0a),code=f.Find(2),prefix=f.Find(0xf),color=f.Find(0x19);
    if(!model||!code||!prefix||!color||code->size()!=2||color->size()!=1||model->empty()||model->size()>32)return {};
    std::string full(model->begin(),model->end()),pre(prefix->begin(),prefix->end());
    if(full.size()!=pre.size()+7||!full.starts_with(pre+"-")||pre.empty())return {};
    for(char c:pre)if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')))return {};
    std::string id=full.substr(pre.size()+1);
    for(char c:id)if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')))return {};
    static constexpr char hex[]="0123456789ABCDEF";
    std::string suffix;for(auto v:*code){suffix+=hex[v>>4];suffix+=hex[v&15];}
    if(id=="00000A"||id.substr(2)!=suffix)return {};
    std::string sub;sub+=hex[(*color)[0]>>4];sub+=hex[(*color)[0]&15];
    return Identity{id,sub,ModelFromResource(id)};
}
std::optional<std::array<Battery,3>> ReadBattery(const Frame& f){
    if(f.service!=1||(f.command!=8&&f.command!=0x27))return {};
    auto levels=f.Find(2),charging=f.Find(3);if(!levels||levels->size()<2)return {};
    std::array<Battery,3> out{};bool any=false;
    for(size_t i=0;i<3&&i<levels->size();++i)if((*levels)[i]<=100){out[i].level=(*levels)[i];any=true;
        out[i].charging=charging&&i<charging->size()&&(*charging)[i]!=0;}
    return any?std::optional(out):std::nullopt;
}
Bytes BatteryQuery(){return Packet(1,8,{{1,{}},{2,{}},{3,{}}});}
Bytes IdentityQuery(){std::vector<Field> fields;for(uint8_t i=1;i<=12;++i)fields.push_back({i,{}});
    for(uint8_t i:{uint8_t{0xf},uint8_t{0x18},uint8_t{0x19}})fields.push_back({i,{}});return Encode(1,7,fields);}
std::optional<int> Control::Parse(const Frame& f) const {
    if(f.service!=service||f.command!=command)return {};
    for(auto const& [tag,wanted]:selectors){auto actual=f.Find(tag);if(!actual||*actual!=wanted)return {};}
    auto bytes=f.Find(field);if(!bytes||bytes->empty())return {};
    if(command==0x2a&&bytes->size()!=2)return {};
    if(command!=0x2a&&command!=0xb4&&bytes->size()!=1)return {};
    if(requiredNoiseMode&&(bytes->size()!=2||(*bytes)[1]!=*requiredNoiseMode))return {};
    size_t offset=index<0?bytes->size()-1:size_t(index);if(offset>=bytes->size())return {};
    if(mirrorField){auto other=f.Find(*mirrorField);if(!other||other->size()!=1||(*other)[0]!=(*bytes)[offset])return {};}
    int value=(*bytes)[offset];
    if(id=="equalizer"&&value>=100&&value<=102)return value;
    for(auto const& option:choices)if(option.value==value)return value;return {};
}
namespace {
bool OneOf(Model m,std::initializer_list<Model> ms){return std::find(ms.begin(),ms.end(),m)!=ms.end();}
Choice Option(int value,std::wstring text,uint8_t s,uint8_t c,std::initializer_list<Field> f){return {value,std::move(text),Packet(s,c,f)};}
Control Boolean(std::string id,std::wstring title,uint8_t write,uint8_t read,uint8_t field=1){
    Control c{std::move(id),std::move(title),L"使用设置",{},Packet(0x2b,read,{{field,{}}}),0x2b,read,field};
    c.choices={Option(0,L"关闭",0x2b,write,{{1,{0}}}),Option(1,L"开启",0x2b,write,{{1,{1}}})};return c;
}
Control Feature(std::string id,std::wstring title,uint8_t slot,int index=-1){
    Control c{std::move(id),std::move(title),L"使用设置",{},Packet(0x2b,0xb4,{{1,{slot}},{2,{}}}),0x2b,0xb4,2,index};
    c.selectors={{1,{slot}}};
    if(slot==0xb||slot==0x10)c.query=Packet(0x2b,0xb4,{{1,{slot}}});
    c.choices={Option(0,L"关闭",0x2b,0xb4,{{1,{slot}},{2,{0}}}),Option(1,L"开启",0x2b,0xb4,{{1,{slot}},{2,{1}}})};return c;
}
void Tap(std::vector<Control>& out,Model m,bool triple,std::vector<std::pair<int,std::wstring>> actions){
    if(actions.empty())return;
    for(uint8_t side=1;side<=2;++side){
        const auto read=uint8_t(triple?0x26:0x20),write=uint8_t(triple?0x25:0x1f);
        Control c{std::string(triple?"triple":"double")+std::to_string(side),
            std::wstring(side==1?L"左耳":L"右耳")+(triple?L"三击":L"双击"),L"手势",{},Packet(1,read,{{1,{}},{2,{}}}),1,read,side};
        for(auto const& [v,label]:actions)c.choices.push_back(Option(v,label,1,write,{{side,{uint8_t(v)}}}));
        out.push_back(std::move(c));
    }
    (void)m;
}
}
std::vector<Control> ControlProfile(Model m){
    using M=Model;std::vector<Control> out;
    if(m==M::Unknown||m==M::Clip||m==M::Eyewear)return out;
    const bool anc=OneOf(m,{M::Buds3,M::Buds4,M::Buds4E,M::Buds5,M::Buds5i,M::Se4Anc,M::Buds6i,M::Buds6,M::Pro3,M::Pro4,M::Pro5,M::Buds7i,M::Buds7});
    const bool transparency=OneOf(m,{M::Buds5i,M::Se4Anc,M::Buds6i,M::Pro3,M::Pro5,M::Buds7i,M::Buds7});
    if(anc){
        Control c{"noise",L"噪声控制",L"声音",{},Packet(0x2b,0x2a,{{1,{}}}),0x2b,0x2a,1,1};
        if(m==M::Buds3){c.query.clear();c.choices={Option(0,L"关闭",0x2b,4,{{1,{0}}}),Option(1,L"降噪",0x2b,4,{{1,{1}}})};}
        else{c.choices={Option(0,L"关闭",0x2b,4,{{1,{0,0}}}),Option(1,L"降噪",0x2b,4,{{1,{1,uint8_t(m==M::Se4Anc?1:255)}}})};
            if(transparency)c.choices.push_back(Option(2,L"通透",0x2b,4,{{1,{2,uint8_t(OneOf(m,{M::Pro5,M::Buds6i,M::Buds7})?2:255)}}}));
            if(m==M::Pro4)c.query.clear();}
        out.push_back(std::move(c));
        if(m==M::Buds3){Control dial{"anc_direction",L"降噪方向",L"声音"};for(int i=0;i<9;++i)dial.choices.push_back(Option(i,std::to_wstring(i+1),0x2b,8,{{1,{uint8_t(i)}}}));out.push_back(std::move(dial));}
        else if(m!=M::Pro4){
            Control level{"anc_level",L"降噪强度",L"声音",{},Packet(0x2b,0x2a,{{1,{}}}),0x2b,0x2a,1};level.requiredNoiseMode=1;
            std::vector<std::pair<int,std::wstring>> options;
            if(OneOf(m,{M::Buds4,M::Buds4E}))options={{1,L"轻度"},{0,L"均衡"}};
            else if(OneOf(m,{M::Pro3,M::Buds7i}))options={{1,L"智慧动态"},{0,L"轻度"},{2,L"均衡"},{3,L"深度"}};
            else{options={{3,L"智慧动态"},{1,L"轻度"},{0,L"均衡"}};if(!OneOf(m,{M::Buds5,M::Buds6,M::Buds7}))options.push_back({2,L"深度"});}
            for(auto const& [v,label]:options)level.choices.push_back(Option(v,label,0x2b,4,{{1,{1,uint8_t(v)}}}));out.push_back(std::move(level));
        }
        if(OneOf(m,{M::Buds6i,M::Pro3,M::Pro5})){
            Control t{"transparency",L"通透模式",L"声音",{},Packet(0x2b,0x2a,{{1,{}}}),0x2b,0x2a,1};t.requiredNoiseMode=2;
            auto values=m==M::Pro3?std::vector<std::pair<int,std::wstring>>{{255,L"通透"},{1,L"人声增强"}}:
                std::vector<std::pair<int,std::wstring>>{{2,L"通透"},{1,L"透传人声"}};
            if(m==M::Pro5)values.push_back({4,L"智慧动态透传"});
            for(auto const& [v,label]:values)t.choices.push_back(Option(v,label,0x2b,4,{{1,{2,uint8_t(v)}}}));out.push_back(std::move(t));
        }
    }
    if(!OneOf(m,{M::Buds3,M::Se4Anc,M::Pro4,M::Arc,M::Eyewear2}))out.push_back(Boolean("wear",L"佩戴检测",0x10,0x11));
    if(OneOf(m,{M::Buds5,M::Buds5i,M::Se4Anc,M::Buds6i,M::Buds6,M::Pro3,M::Pro5,M::Buds7i,M::Buds7,M::Clip2,M::Eyewear2}))
        out.push_back(Boolean("latency",L"低时延",0x6c,0x6c,2));
    if(OneOf(m,{M::Buds5,M::Buds5i,M::Pro5,M::Buds7i,M::Buds7}))out.push_back(Boolean("quality",L"高清音质",0xa2,0xa3,2));
    if(OneOf(m,{M::Buds6,M::Pro5,M::Buds7,M::Clip2})){
        out.push_back(Feature("adaptive_volume",L"自适应音量",2));
        if(m!=M::Buds6)out.push_back(Boolean("dual",L"双设备连接",0x2e,0x2f));
        auto c=Boolean("case_sound",L"充电盒提示音",0xb1,0xb1,2);c.query=Packet(0x2b,0xb1,{{2,{}},{3,{}}});
        if(m==M::Pro5){c.service=0x2b;c.command=0xb4;c.field=2;c.index=0;c.selectors={{1,{0x10}}};c.query=Packet(0x2b,0xb4,{{1,{0x10}}});}
        out.push_back(std::move(c));
    }
    if(OneOf(m,{M::Buds6,M::Buds7,M::Clip2}))out.push_back(Feature("drop",L"掉落提醒",7));
    if(OneOf(m,{M::Buds7i,M::Pro5,M::Buds7,M::Clip2}))out.push_back(Feature("head",L"头动控制",0xb));
    if(m==M::Buds7)out.push_back(Feature("single_anc",L"单耳降噪",5));
    if(m==M::Se4Anc)out.push_back(Feature("wind",L"防风噪",0x1d));
    if(m==M::Pro5){out.push_back(Feature("voice",L"语音控制",3,0));
        Control tips=Feature("tips",L"耳塞材质",8);tips.choices={Option(1,L"硅胶",0x2b,0xb4,{{1,{8}},{2,{1}}}),Option(2,L"海绵",0x2b,0xb4,{{1,{8}},{2,{2}}})};out.push_back(std::move(tips));}
    if(m==M::Clip2)out.push_back(Boolean("noisy_volume",L"嘈杂环境大音量",0x87,0x88));
    if(OneOf(m,{M::Buds7i,M::Pro5,M::Buds7,M::Clip2})){
        Control spatial=Feature("spatial",L"空间音频",0x18,0);spatial.group=L"声音";
        spatial.query=Packet(0x2b,0xb4,{{1,{0x18}},{2,{}},{3,{}}});
        // Current verified captures use 1=head tracking and 2=fixed.
        const bool reverse=false;
        spatial.choices={Option(0,L"关闭",0x2b,0xb4,{{1,{0x18}},{2,{0}}}),
            Option(reverse?1:2,L"固定",0x2b,0xb4,{{1,{0x18}},{2,{2}}}),
            Option(reverse?2:1,L"头部跟踪",0x2b,0xb4,{{1,{0x18}},{2,{1}}})};out.push_back(std::move(spatial));
        if(m==M::Clip2){
        Control scene=Feature("scene",L"空间音频场景",0x18,0);scene.group=L"声音";scene.field=3;
        scene.query=Packet(0x2b,0xb4,{{1,{0x18}},{2,{}},{3,{}}});scene.choices.clear();
        const wchar_t* names[]{L"默认",L"有声剧场",L"影院",L"音乐厅"};
        for(int i=0;i<4;++i)scene.choices.push_back(Option(i,names[i],0x2b,0xb4,{{1,{0x18}},{3,{uint8_t(i)}}}));out.push_back(std::move(scene));
        }
    }
    std::vector<std::pair<int,std::wstring>> presets;
    if(OneOf(m,{M::Buds4,M::Buds4E,M::Buds5,M::Buds5i,M::Se4Anc,M::Buds6i,M::Buds7i,M::Clip2}))presets={{1,L"默认"},{2,L"低音增强"},{3,L"高音增强"}};
    if(OneOf(m,{M::Buds5,M::Buds5i,M::Se4Anc,M::Buds6i,M::Buds7i,M::Clip2}))presets.push_back({9,L"人声"});
    if(m==M::Clip2)presets={{1,L"默认"},{10,L"动感增强"},{3,L"高音增强"},{9,L"人声"}};
    if(m==M::Buds6)presets={{0,L"原声"},{2,L"低音增强"},{3,L"高音增强"},{9,L"人声"}};
    if(m==M::Arc)presets={{1,L"默认"},{10,L"动感"},{2,L"低音增强"},{3,L"高音增强"},{9,L"人声"}};
    if(OneOf(m,{M::Pro5,M::Buds7})){presets={{5,L"均衡"},{9,L"人声"},{2,L"低音"},{13,L"电影"},{15,L"播客"},{14,L"游戏"},{16,L"运动"}};if(m==M::Pro5)presets.push_back({17,L"AI 音效"});}
    if(!presets.empty()){
        Control eq{"equalizer",L"音效",L"声音",{},Packet(0x2b,0x4a,{{2,{}}}),0x2b,0x4a,2};
        for(auto const& [v,label]:presets)eq.choices.push_back(Option(v,label,0x2b,0x49,{{1,{uint8_t(v)}}}));if(OneOf(m,{M::Pro5,M::Buds7}))eq.choices.push_back(Option(201,L"古典",0x2b,0x49,
            {{1,{201}},{2,{10}},{5,{1}},{3,{251,20,30,10,0,0,231,246,10,0}},{4,Bytes{'2','0','1'}}}));
        out.push_back(std::move(eq));
    }
    std::vector<std::pair<int,std::wstring>> doubleTap,tripleTap;
    if(m==M::Buds3)doubleTap={{1,L"播放 / 暂停"},{4,L"下一首"},{0,L"语音助手"},{3,L"降噪切换"},{255,L"无操作"}};
    else if(m==M::Buds6i)doubleTap={{1,L"播放 / 暂停"},{2,L"下一首"}};
    else if(m==M::Eyewear2)doubleTap={{1,L"播放 / 暂停"},{0,L"语音助手"},{255,L"无操作"}};
    else if(OneOf(m,{M::Buds4,M::Buds4E,M::Buds5i,M::Buds7i,M::Arc,M::Clip2})){
        doubleTap={{1,L"播放 / 暂停"},{2,L"下一首"},{0,L"语音助手"},{255,L"无操作"}};
        if(m==M::Clip2)doubleTap.push_back({7,L"空间音频"});else doubleTap.push_back({7,L"上一首"});}
    if(OneOf(m,{M::Buds6i,M::Pro5,M::Buds7i,M::Buds7,M::Clip2,M::Arc}))tripleTap={{2,L"下一首"},{7,L"上一首"},{255,L"无操作"}};
    Tap(out,m,false,doubleTap);Tap(out,m,true,tripleTap);
    if(OneOf(m,{M::Buds4,M::Buds4E,M::Buds6i,M::Pro3,M::Buds7i,M::Arc}))for(uint8_t side=1;side<=2;++side){
        Control c{"hold"+std::to_string(side),std::wstring(side==1?L"左耳":L"右耳")+L"长按",L"手势",{},Packet(0x2b,0x17,{{1,{}},{2,{}}}),0x2b,0x17,side};
        c.choices={Option(0,L"语音助手",0x2b,0x16,{{side,{0}}}),Option(255,L"无操作",0x2b,0x16,{{side,{255}}})};
        if(m!=M::Arc)c.choices.push_back(Option(10,L"噪声控制",0x2b,0x16,{{side,{10}}}));
        if(OneOf(m,{M::Buds4,M::Buds4E})){c.choices.erase(c.choices.begin());c.choices.push_back(Option(14,L"识别歌曲",0x2b,0x16,{{side,{14}}}));}out.push_back(std::move(c));
    }
    if(OneOf(m,{M::Buds6i,M::Clip2,M::Arc}))for(uint8_t side=1;side<=2;++side){
        Control c{"swipe"+std::to_string(side),std::wstring(side==1?L"左耳":L"右耳")+L"滑动",L"手势",{},Packet(0x2b,0x1f,{{1,{}},{2,{}}}),0x2b,0x1f,side};
        c.choices.push_back(Option(0,L"调节音量",0x2b,0x1e,{{side,{0}}}));
        if(m!=M::Clip2)c.choices.push_back(Option(1,L"切换歌曲",0x2b,0x1e,{{side,{1}}}));
        if(m!=M::Arc)c.choices.push_back(Option(255,L"无操作",0x2b,0x1e,{{side,{255}}}));out.push_back(std::move(c));
    }
    if(OneOf(m,{M::Pro3,M::Pro5,M::Buds7})){
        const wchar_t* labels[]{L"捏合接听 / 结束通话",L"捏合拒接",L"捏合播放 / 暂停",L"捏合下一首",L"捏合上一首"};
        for(int i=(m==M::Buds7?2:0);i<5;++i){
            const uint8_t slot=uint8_t(i<2?i:i-2),context=uint8_t(i<2?1:2);
            const uint8_t action=uint8_t(i==0?0:i==1?1:i==2?2:i==3?4:3);
            Control c{"pinch"+std::to_string(i),labels[i],L"手势",{},Packet(0x2b,0x93,{{1,{slot}},{2,{context}}}),0x2b,0x93,3};
            c.selectors={{1,{slot}},{2,{context}}};c.mirrorField=4;
            c.choices={Option(255,L"关闭",0x2b,0x92,{{1,{slot}},{2,{context}},{3,{255}},{4,{255}}}),
                Option(action,L"开启",0x2b,0x92,{{1,{slot}},{2,{context}},{3,{action}},{4,{action}}})};out.push_back(std::move(c));
        }
    }
    if(m==M::Buds7){
        Control c{"double_enabled",L"双击播放 / 暂停",L"手势",{},Packet(1,0x20,{{1,{}},{2,{}}}),1,0x20,1};c.mirrorField=2;
        c.choices={Option(255,L"关闭",1,0x1f,{{1,{255}},{2,{255}}}),Option(1,L"开启",1,0x1f,{{1,{1}},{2,{1}}})};out.push_back(std::move(c));
    }
    if(OneOf(m,{M::Pro3,M::Pro5,M::Buds7,M::Buds7i})){
        Control c{"swipe_volume",L"滑动调节音量",L"手势",{},Packet(0x2b,0x1f,{{1,{}},{2,{}}}),0x2b,0x1f,1};c.mirrorField=2;
        for(uint8_t action:{uint8_t{255},uint8_t{0}}){
            // Captured legacy setter uses a side descriptor (02 02 value),
            // not the response's TLV representation. Keep this exact envelope;
            // do not generalize it into an arbitrary raw-packet API.
            Bytes bytes{0x5a,0,9,0,0x2b,0x1e,1,1,action,2,2,action};
            auto crc=Crc(bytes);bytes.push_back(uint8_t(crc>>8));bytes.push_back(uint8_t(crc));
            c.choices.push_back({action,action==255?L"关闭":L"开启",std::move(bytes)});
        }
        out.push_back(std::move(c));
    }
    if(m==M::Clip2)for(uint8_t tag:{uint8_t{3},uint8_t{4}}){
        Control c=Feature(tag==3?"nod":"shake",tag==3?L"点头来电动作":L"摇头来电动作",0xb,0);c.group=L"手势";c.field=tag;c.choices.clear();
        const wchar_t* names[]{L"无操作",L"接听来电",L"拒接来电"};
        for(uint8_t value=0;value<3;++value)c.choices.push_back(Option(value,names[value],0x2b,0xb4,{{1,{0xb}},{tag,{value}}}));out.push_back(std::move(c));
    }
    return out;
}
std::optional<Bytes> WriteControl(Model m,std::string_view id,int value){
    for(auto const& c:ControlProfile(m))if(c.id==id)for(auto const& o:c.choices)if(o.value==value)return o.write;
    return {};
}
bool SupportsCustomEqualizer(Model m){return OneOf(m,{Model::Se4Anc,Model::Buds6i,Model::Pro5,Model::Buds7,Model::Buds7i,Model::Arc});}
std::optional<Bytes> CustomEqualizer(Model m,int slot,std::string_view name,std::span<const int> gains){
    if(!SupportsCustomEqualizer(m)||slot<100||slot>102||name.empty()||name.size()>32||gains.size()!=10)return {};
    Bytes curve;for(int v:gains){if(v< -60||v>60)return {};curve.push_back(uint8_t(v));}
    return Packet(0x2b,0x49,{{1,{uint8_t(slot)}},{2,{10}},{5,{uint8_t(m==Model::Buds7i?0:1)}},{3,curve},{4,Bytes(name.begin(),name.end())}});
}
std::optional<std::array<int,10>> ReadEqualizer(const Frame& f){
    if(f.service!=0x2b||f.command!=0x4a)return {};
    auto data=f.Find(6);
    Bytes selected;
    if(!data){
        auto records=f.Find(8),id=f.Find(2);
        if(!records||records->size()%36||!id||id->size()!=1)return {};
        for(size_t start=0;start<records->size();start+=36){
            if((*records)[start]==(*id)[0]&&(*records)[start+1]==10){
                if(!selected.empty())return {};
                selected.assign(records->begin()+start+2,records->begin()+start+12);
            }
        }
        data=&selected;
    }
    if(data->size()!=10)return {};
    std::array<int,10> out;for(size_t i=0;i<10;++i){out[i]=int8_t((*data)[i]);if(out[i]< -60||out[i]>60)return {};}return out;
}
std::optional<PairedPeer> ReadPairedPeer(const Frame& f){
    if(f.service!=0x2b||f.command!=0x31)return {};
    auto address=f.Find(4),connected=f.Find(5),name=f.Find(9);
    if(!address||address->size()!=6||!connected||connected->size()!=1||(*connected)[0]>1)return {};
    PairedPeer out;std::copy(address->begin(),address->end(),out.address.begin());out.connected=(*connected)[0]!=0;
    if(name&&name->size()<=128)out.name.assign(name->begin(),name->end());return out;
}
Bytes PairedPeersQuery(Model model){return model==Model::Buds7i?Packet(0x2b,0x31,{{1,{}},{0xd,{0}}}):Bytes{};}
std::optional<Bytes> ForgetPeer(Model model,const std::array<uint8_t,6>& address){
    if(model!=Model::Buds7i||std::all_of(address.begin(),address.end(),[](uint8_t v){return v==0;}))return {};
    return Packet(0x2b,0x31,{{4,Bytes(address.begin(),address.end())}});
}
}
