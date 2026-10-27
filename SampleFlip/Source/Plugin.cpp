#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <random>

class SampleFlipProcessor : public juce::AudioProcessor {
public:
    SampleFlipProcessor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        params(*this, nullptr, "PARAMETERS", {
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"pitch",1},"Pitch",-12.f,12.f,0.f),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"speed",1},"Speed",0.5f,2.f,1.f),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"cutoff",1},"Cutoff",150.f,18000.f,7000.f),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"drive",1},"Drive",0.f,1.f,0.15f),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"crush",1},"Crush",0.f,1.f,0.f),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"swing",1},"Swing",0.f,0.75f,0.15f),
            std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"style",1},"Style",juce::StringArray{"Lo-Fi Hip-Hop","90s House"},0),
            std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"bpm",1},"BPM",65.f,160.f,95.f)
        }) { formats.registerBasicFormats(); generatePattern(); }
    const juce::String getName() const override {return "Sample Flip";}
    bool acceptsMidi() const override {return true;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 0;}
    bool isMidiEffect() const override {return false;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return {};}
    void changeProgramName(int,const juce::String&) override {}
    void prepareToPlay(double sr,int) override { sampleRate=sr; resetVoices(); lp[0]=lp[1]=0; stepCounter=0; currentStep=-1; }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {return b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
    void processBlock(juce::AudioBuffer<float>& out,juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals nd; out.clear();
        const juce::SpinLock::ScopedTryLockType lock(sampleLock);
        if(!lock.isLocked() || audio.getNumSamples()<2) return;
        auto* pitch=params.getRawParameterValue("pitch"); auto* speed=params.getRawParameterValue("speed");
        auto* cutoff=params.getRawParameterValue("cutoff"); auto* drive=params.getRawParameterValue("drive");
        auto* crush=params.getRawParameterValue("crush"); auto* swing=params.getRawParameterValue("swing");
        auto* bpm=params.getRawParameterValue("bpm"); auto* style=params.getRawParameterValue("style");
        double ratio=std::pow(2.0,(double)pitch->load()/12.0)*speed->load()*fileRate/sampleRate;
        double c=1.0-std::exp(-2.0*juce::MathConstants<double>::pi*cutoff->load()/sampleRate);
        double beatSamples=sampleRate*60.0/juce::jmax(1.f,bpm->load());
        double stepLength=beatSamples/4.0;
        juce::MidiBuffer::Iterator it(midi); juce::MidiMessage message; int eventSample=0;
        bool hasEvent=it.getNextEvent(message,eventSample);
        for(int i=0;i<out.getNumSamples();++i) {
            while(hasEvent && eventSample<=i) {
                if(message.isNoteOn()) trigger(message.getNoteNumber()%16, message.getFloatVelocity());
                hasEvent=it.getNextEvent(message,eventSample);
            }
            if(playingPattern) {
                int step=(int)std::floor(stepCounter/stepLength)%16;
                double phase=std::fmod(stepCounter,stepLength);
                double delay=(step%2==1 ? swing->load()*stepLength*0.5 : 0.0);
                if(step!=currentStep && phase>=delay) {trigger(pattern[(size_t)step],0.8f);currentStep=step;}
                if(step!=currentStep && phase<delay) {} // delay odd steps
                stepCounter+=1.0;
                if(stepCounter>=stepLength*16) {stepCounter=0;currentStep=-1;}
            }
            for(int ch=0;ch<2;++ch) {
                double mix=0;
                for(auto& v:voices) if(v.active) {
                    int a=(int)v.pos; if(a>=v.end-1) continue;
                    double frac=v.pos-a;
                    const float* src=audio.getReadPointer(juce::jmin(ch,audio.getNumChannels()-1));
                    double fade=juce::jmin(1.0,(v.end-v.pos)/(fileRate*0.008));
                    mix+=(src[a]*(1-frac)+src[a+1]*frac)*v.velocity*fade;
                }
                lp[ch]+=c*(mix-lp[ch]);
                double d=1.0+drive->load()*12.0;
                double val=std::tanh(lp[ch]*d)/std::tanh(d);
                if(crush->load()>0.01f) {double levels=std::pow(2.0,16.0-crush->load()*12.0);val=std::round(val*levels)/levels;}
                out.setSample(ch,i,(float)val);
            }
            for(auto& v:voices) if(v.active) {v.pos+=ratio;if(v.pos>=v.end-1)v.active=false;}
        }
        (void)style;
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    void getStateInformation(juce::MemoryBlock& dest) override {auto state=params.copyState();state.setProperty("path",filePath,nullptr);state.setProperty("pattern",patternString(),nullptr);state.setProperty("auto",playingPattern,nullptr);if(auto xml=state.createXml())copyXmlToBinary(*xml,dest);}
    void setStateInformation(const void* data,int size) override {if(auto xml=getXmlFromBinary(data,size)) {auto state=juce::ValueTree::fromXml(*xml);if(state.isValid()) {params.replaceState(state);filePath=state.getProperty("path").toString();playingPattern=(bool)state.getProperty("auto",false);setPatternString(state.getProperty("pattern").toString());if(filePath.isNotEmpty())loadFile(juce::File(filePath));}}}
    bool loadFile(const juce::File& file) {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if(!reader || reader->lengthInSamples<16 || reader->lengthInSamples>44100LL*60*15) return false;
        juce::AudioBuffer<float> next((int)juce::jmin((unsigned int)2,reader->numChannels),(int)reader->lengthInSamples);
        reader->read(&next,0,next.getNumSamples(),0,true,true);
        const juce::SpinLock::ScopedLockType lock(sampleLock);
        audio=std::move(next);fileRate=reader->sampleRate;filePath=file.getFullPathName();resetVoices();return true;
    }
    void trigger(int slice,float velocity) {
        if(audio.getNumSamples()<2)return;
        auto& v=voices[(size_t)(voiceCursor++%voices.size())];
        int size=juce::jmax(2,audio.getNumSamples()/16);
        v.pos=(double)(juce::jlimit(0,15,slice)*size);v.end=juce::jmin(audio.getNumSamples(),(juce::jlimit(0,15,slice)+1)*size);
        v.velocity=velocity;v.active=true;
    }
    void generatePattern() {
        std::mt19937 gen(std::random_device{}());std::uniform_int_distribution<int> dist(0,15);
        bool house=params.getRawParameterValue("style")->load()>0.5f;
        for(int i=0;i<16;++i)pattern[(size_t)i]=house?(i%4==0?dist(gen):((i%4==2)?dist(gen):pattern[(size_t)(i/4*4)])):dist(gen);
        stepCounter=0;currentStep=-1;
    }
    juce::String patternString() const {juce::String s;for(auto p:pattern)s+=juce::String(p)+",";return s;}
    void setPatternString(juce::String s) {auto a=juce::StringArray::fromTokens(s,",","");for(int i=0;i<juce::jmin(16,a.size());++i)pattern[(size_t)i]=juce::jlimit(0,15,a[i].getIntValue());}
    juce::AudioProcessorValueTreeState params;
    bool playingPattern=false;
    juce::String filePath;
private:
    struct Voice {double pos=0;int end=0;float velocity=0;bool active=false;};
    std::array<Voice,16> voices{};std::array<int,16> pattern{};
    int voiceCursor=0,currentStep=-1;double stepCounter=0,sampleRate=44100,fileRate=44100,lp[2]={0,0};
    juce::SpinLock sampleLock;juce::AudioBuffer<float> audio;juce::AudioFormatManager formats;
    void resetVoices(){for(auto& v:voices)v.active=false;}
};
class SampleFlipEditor : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit SampleFlipEditor(SampleFlipProcessor& p):AudioProcessorEditor(p),processor(p) {
        setSize(750,430);
        addAndMakeVisible(header);header.setText("SAMPLE FLIP  /  SOUL TO GROOVE",juce::dontSendNotification);header.setFont(juce::Font(juce::FontOptions(25.f,juce::Font::bold)));
        addAndMakeVisible(info);info.setText("Drop WAV / AIFF / MP3 here, or click LOAD",juce::dontSendNotification);
        addAndMakeVisible(load);load.setButtonText("LOAD SAMPLE");load.onClick=[this]{chooser=std::make_unique<juce::FileChooser>("Load a sample",juce::File{},"*.wav;*.aif;*.aiff;*.mp3");chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c){auto f=c.getResult();if(f.existsAsFile())loadSample(f);});};
        addAndMakeVisible(generate);generate.setButtonText("GENERATE FLIP");generate.onClick=[this]{processor.generatePattern();repaint();};
        addAndMakeVisible(play);play.setButtonText("PLAY SEQUENCE");play.onClick=[this]{processor.playingPattern=!processor.playingPattern;play.setButtonText(processor.playingPattern?"STOP SEQUENCE":"PLAY SEQUENCE");};
        const char* ids[]={"pitch","speed","cutoff","drive","crush","swing","bpm"};
        const char* names[]={"PITCH","SPEED","FILTER","DRIVE","CRUSH","SWING","BPM"};
        for(int i=0;i<7;++i){auto& s=sliders[(size_t)i];addAndMakeVisible(s);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,20);attachments[(size_t)i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.params,ids[i],s);auto& l=labels[(size_t)i];addAndMakeVisible(l);l.setText(names[i],juce::dontSendNotification);l.setJustificationType(juce::Justification::centred);}
        addAndMakeVisible(style);style.addItem("LO-FI HIP-HOP",1);style.addItem("90s HOUSE",2);styleAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.params,"style",style);
        startTimerHz(10);
    }
    void resized() override {
        header.setBounds(25,15,600,40);info.setBounds(25,57,680,25);load.setBounds(25,94,180,35);style.setBounds(220,94,160,35);generate.setBounds(395,94,155,35);play.setBounds(565,94,160,35);
        for(int i=0;i<7;++i){int x=18+i*103;labels[(size_t)i].setBounds(x,160,98,22);sliders[(size_t)i].setBounds(x,184,98,110);}
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(20,22,23));g.setColour(juce::Colour::fromRGB(235,160,80));g.drawLine(20,143,730,143,2);
        g.setFont(15.f);g.drawText("16 SLICES  •  MIDI NOTES C1–D#2 (note numbers 36–51)",25,315,680,24,juce::Justification::left);
        for(int i=0;i<16;++i){int x=25+i*44;g.setColour(juce::Colour::fromRGB(45+(i%4)*9,49,50));g.fillRoundedRectangle((float)x,353,39.f,39.f,5.f);g.setColour(juce::Colours::white);g.drawText(juce::String(i+1),x,355,39,33,juce::Justification::centred);}
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override {return files.size()>0;}
    void filesDropped(const juce::StringArray& files,int,int) override {if(!files.isEmpty())loadSample(juce::File(files[0]));}
private:
    void timerCallback() override {}
    void loadSample(const juce::File& f){info.setText(processor.loadFile(f)?"LOADED: "+f.getFileName():"Could not load sample",juce::dontSendNotification);}
    SampleFlipProcessor& processor;juce::Label header,info;juce::TextButton load,generate,play;
    juce::ComboBox style;std::unique_ptr<juce::FileChooser> chooser;
    std::array<juce::Slider,7> sliders;std::array<juce::Label,7> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,7> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment;
};
juce::AudioProcessorEditor* SampleFlipProcessor::createEditor(){return new SampleFlipEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new SampleFlipProcessor();}
