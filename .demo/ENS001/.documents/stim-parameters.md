# Electrostimulation Parameters by Application

Summary table of parameters for all electrostimulation types supported by ENS001.

Each mode is implemented as a `constexpr StimMode` in the corresponding `Modes/*.h` file.

VNS parameters are based on `vns_parameters.xlsx`. TENS/EMS/IFT parameters are based on clinical guidelines and meta-analyses.


---


# VNS — Vagus Nerve Stimulation


## Epilepsy

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Classic Protocol | `Epilepsy::Classic` | 20–30 | 130–500 | 1.0–3.0 | 30 s ON / 5 min OFF | Chronic | Left cervical vagus | [Johnson & Wilson 2018; Panebianco 2016](https://pmc.ncbi.nlm.nih.gov/articles/PMC8313807/) |
| Optimal (Norway 2025) | `Epilepsy::OptimalNorway` | 20 | 250 | 1.5–2.25 | 30 s ON / 5 min OFF | Chronic | Left cervical vagus | [NOR-current study 2025](https://www.sciencedirect.com/science/article/pii/S1935861X25002670) |
| tVNS Auricular | `Epilepsy::AuricularTragus` | 10–25 | 300 | By pain threshold | 3 sessions x 1 h/day | 1 h | Left ear tragus | Stefan et al. 2012 |


## Depression

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Implantable VNS | `Depression::Implantable` | 30 | 250–500 | 0.25–1.5 | 30 s ON / 5 min OFF | Chronic | Left cervical vagus | Pardo et al.; FDA approved |
| taVNS Auricular | `Depression::Auricular` | 20 | 200–500 | 0.5–1.0 | 30 s ON / 30 s OFF | 30 min | Cymba conchae, left ear | [Rong et al. 2016](https://pmc.ncbi.nlm.nih.gov/articles/PMC4017164/) |


## Stress / Anxiety

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Anxiolytic Effect | `Anxiety::Anxiolytic` | 20–25 | 80–250 | 0.5–2.0 | 30 s ON / 30 s OFF | 30 min | Cymba conchae / tragus | Burger 2019; Grolaux 2019 |
| HRV Optimum | `Anxiety::HrvOptimal` | 10 | 250–500 | By sensation | Continuous | 15 min | Cymba conchae, left ear | [Kim et al. 2025 (MDPI)](https://www.mdpi.com/2227-9059/13/3/700) |


## Sleep / Insomnia

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Sleep Improvement | `Sleep::Improvement` | 20–30 | 200 | 1.0 | Continuous | 20 min x 2/day | Auricular concha, bilateral | Bretherton 2019; Jiao 2020 |
| Primary Insomnia | `Sleep::Insomnia` | 20 | 200 | 1.0 | Continuous | 20 min x 2/day, 1 month | Cymba conchae + cavum | [Xuanwu Hospital 2022](https://pmc.ncbi.nlm.nih.gov/articles/PMC9599790/) |


## Alertness / Anti-Drowsiness

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Cervical (gammaCore) | `Alertness::CervicalBurst` | 25 | 1000 (5 kHz burst) | By sensation | 6 min/dose | 6 min | Neck, cervical branch | [McIntire et al. 2021](https://www.nature.com/articles/s42003-021-02145-7) |
| Auricular | `Alertness::Auricular` | 25 | 200–500 | 0.5–1.0 | Continuous | 20 min | Cymba conchae, left ear | [PMC9859411, 2023](https://pmc.ncbi.nlm.nih.gov/articles/PMC9859411/) |


## Pain (VNS)

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Migraine (gammaCore) | `Pain::MigraineBurst` | 25 | 1000 (5 kHz burst) | By sensation | 2 min x 3 doses | 2 min | Neck, cervical branch | gammaCore / electroCore; FDA approved |
| Chronic Pain | `Pain::Chronic` | 20–25 | 200–500 | 0.5–2.0 | Continuous | 30 min | Cymba conchae or neck | [Meta-analysis 2024](https://pmc.ncbi.nlm.nih.gov/articles/PMC11309651/) |
| Rheumatoid Arthritis | `Pain::RheumatoidArthritis` | 20 | 500 | 1.0 | Continuous | 30 min | Auricular | Koopman et al. 2016 |


## Inflammation

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Anti-Inflammatory | `Inflammation::AntiInflammatory` | 1–10 | 250–500 | 0.5–2.0 | Variable | 30 min | Cymba conchae | Generally accepted mechanism (cholinergic anti-inflammatory pathway) |


## Appetite / Obesity

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Cervical Stimulation | `Appetite::CervicalStimulation` | 30 | 500 | 0.25–1.5 | 30 s ON / 5 min OFF | Chronic | Cervical vagus | [Pardo et al.](https://pmc.ncbi.nlm.nih.gov/articles/PMC2365729/) |
| Abdominal (Satiety) | `Appetite::AbdominalSatiety` | ~50 | Variable | Variable | Continuous | 30 min | Abdominal vagus | Val-Laillet 2010; Krolczyk et al. |
| tVNS for Food Cues | `Appetite::FoodCueModulation` | 25 | 200–500 | 0.6 | 30 s ON / 30 s OFF | 2 h | Cymba conchae (NEMOS) | Frontiers Hum Neurosci 2020 |


## Tinnitus

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| tVNS + Sound Therapy | `Tinnitus::PairedSoundTherapy` | 25 | 250–500 | Up to pain threshold | Variable | 30 min | Tragus / cymba conchae | [Engineer 2011; Shim 2015](https://pmc.ncbi.nlm.nih.gov/articles/PMC8193498/) |


## Cognitive Enhancement

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Associative Memory | `Cognitive::Memory` | 25 | 200–500 | 0.5–1.0 | During task | 30 min | Cymba conchae / tragus | Jacobs et al. 2015 |
| Working Memory | `Cognitive::WorkingMemory` | 25 | 200–300 | 0.5–1.0 | During task | 30 min | Cymba conchae | Konjusha et al. 2023 |
| Divergent Thinking | `Cognitive::DivergentThinking` | 25 | 200–300 | 0.5 | During task | 30 min | Left ear | Colzato et al. 2018 |
| Sensory Perception | `Cognitive::SensoryPerception` | 25 | 1000 (5 kHz burst) | By sensation | Continuous | 30 min | Cervical (neck) | Sci Reports 2024 |


## Post-Stroke Rehabilitation

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Motor Recovery | `Rehab::MotorRecovery` | 30 | 100–500 | 0.5–1.8 | Paired with movement | 30 min | Left cervical / auricular | Dawson et al.; FDA approved (Vivistim) |


## Nerve Block (kHz)

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Stimulation Site | Source |
|---|---|---|---|---|---|---|---|---|
| Afferent Block | `NerveBlock::AfferentBlock` | 900–4000 | 10–300+ | 4.0 | >70% | Per application | Abdominal vagus (cuff) | [Payne et al. 2023](https://link.springer.com/article/10.1186/s42234-023-00117-2) |
| Directional Stimulation | `NerveBlock::DirectionalEfferent` | 1000–1600 | >300 | 4.0 | >70% | Per application | Multi-electrode cuff | Springer 2023 |


---


# TENS — Transcutaneous Electrical Nerve Stimulation

Pain relief through surface electrodes. No implantation required.

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Mechanism | Source |
|---|---|---|---|---|---|---|---|---|
| Conventional | `TENS::Conventional` | 80–120 | 50–200 | 10–30 (sensory threshold) | Continuous | 30 min | Gate control theory of pain -- blocking via A-beta fibers | Sluka & Walsh 2003; Johnson 2007 |
| Acupuncture-Like (AL-TENS) | `TENS::AcupunctureLike` | 2–4 | 200–300 | 20–40 (motor threshold) | Continuous | 30 min | Endorphin-mediated analgesia via A-delta fibers | Sluka & Walsh 2003; Vance et al. 2014 |
| Burst TENS | `TENS::Burst` | 2 (burst 100 Hz) | 200 | 20–30 | Continuous | 30 min | Gate control + endorphins (combined) | Johnson 2007 |
| Brief Intense | `TENS::BriefIntense` | 100–150 | 150–250 | Up to 50 (max tolerable) | Continuous | 15 min | For acute/procedural pain -- counter-irritation | Johnson 2007 |


---


# EMS — Electrical Muscle Stimulation

Activation of skeletal muscles through surface electrodes.

| Mode | Code Alias | Frequency (Hz) | Pulse Width (us) | Current (mA) | Duty Cycle | Session | Mechanism | Source |
|---|---|---|---|---|---|---|---|---|
| Strength Training (NMES) | `EMS::Strengthening` | 35–75 | 200–400 | 30–60 (visible contraction) | 10 s ON / 50 s OFF | 30 min | Tetanic contraction of muscle fibers | Maffiuletti 2010; Filipovic et al. 2012 |
| Endurance Training | `EMS::Endurance` | 20–40 | 200–300 | 25–45 (moderate contraction) | 10 s ON / 15 s OFF | 30 min | Recruitment of slow-twitch fibers (Type I) | Filipovic et al. 2012 |
| Recovery / Relaxation | `EMS::Recovery` | 1–5 | 200–300 | 10–20 (light twitches) | Continuous | 20 min | Enhanced blood flow, lymphatic drainage | Maffiuletti 2010 |
| Denervated Muscle | `EMS::Denervated` | 3–5 | 500–1000 | 40–60 | 4 s ON / 8 s OFF | 30 min | Direct activation of muscle fibers (without nerve) | Kern et al. 2010 |


---


# IFT — Interferential Therapy

Two stimulation channels with close carrier frequencies (~4 kHz). Therapeutic effect is determined by the difference (beat) frequency. Requires dual-channel output.

> **Note:** ENS001-A supports IFT via Driver A (4 channels) + Driver B. ENS001-A2 has 2 wide-range channels -- minimal IFT configuration.

| Mode | Code Alias | Beat Frequency (Hz) | Carrier (Hz) | Current (mA) | Duty Cycle | Session | Mechanism | Source |
|---|---|---|---|---|---|---|---|---|
| Pain Relief | `IFT::PainRelief` | 80–120 | 4000 | 20–40 (comfortable tingling) | Continuous | 20 min | Gate control in deep tissues (carrier penetrates deeper) | Fuentes et al. 2010; Beatti et al. 2011 |
| Muscle Stimulation | `IFT::MuscleStim` | 10–25 | 4000 | 25–45 (motor threshold) | 10 s ON / 10 s OFF | 20 min | Motor recruitment in deep muscles | Fuentes et al. 2010 |
| Circulation Improvement | `IFT::Circulation` | 1–10 | 4000 | 15–25 (sub-motor) | Continuous | 20 min | Vasodilation, smooth muscle modulation | Fuentes et al. 2010 |


---


## Safety Limits

### VNS

| Parameter | Recommendation |
|---|---|
| Max frequency (chronic) | 30 Hz for implants. At 50 Hz -- degeneration of myelinated fibers |
| Safe current (transcutaneous) | < 5 mA, by sensation/pain threshold |
| Safe current (implant) | Up to 3.5 mA, typically 1.0–2.5 mA |
| Side (cervical) | **LEFT** -- right vagus innervates the cardiac SA node |
| Side (auricular) | Both acceptable -- no efferent fibers to the heart from ABVN |
| Ear site | Cymba conchae (max vagal innervation) > Tragus > Earlobe (control) |
| Charge balance | **Mandatory** -- biphasic charge-balanced pulses |
| Cardiac risks | Bradycardia at high currents. ECG monitoring recommended |
| Contraindications | Vagotomy, active implants (pacemaker), pregnancy (caution) |

### TENS / EMS

| Parameter | Recommendation |
|---|---|
| Max current | Typically < 80 mA. Adjusted by patient sensation |
| Charge balance | **Mandatory** -- biphasic symmetric or asymmetric balanced |
| Session duration | 15–60 min. Do not exceed 1 h without a break |
| Contraindications | Pacemaker, epilepsy (TENS on head/neck), pregnancy (abdomen), malignant tumors in stimulation area |
| Electrode placement | Do not place on the front of the neck, on the eyes, across the chest |
| EMS: denervated muscles | Requires long pulses (>500 us) and high current -- risk of skin burn |

### IFT

| Parameter | Recommendation |
|---|---|
| Carrier frequency | 4000 Hz standard. 2000–4000 Hz acceptable |
| Max current | < 50 mA per channel |
| Contraindications | Same as TENS + caution with metal implants in stimulation area |


---


## Key Sources

### VNS

| Article | URL |
|---|---|
| Thompson et al. 2021 -- review of all VNS parameters | https://pmc.ncbi.nlm.nih.gov/articles/PMC8313807/ |
| Kim et al. 2025 -- optimal HRV parameters | https://www.mdpi.com/2227-9059/13/3/700 |
| NOR-current 2025 -- current optimization for epilepsy | https://www.sciencedirect.com/science/article/pii/S1935861X25002670 |
| McIntire et al. 2021 -- ctVNS against drowsiness | https://www.nature.com/articles/s42003-021-02145-7 |
| Val-Laillet 2010 -- VNS and obesity | https://www.sciencedirect.com/science/article/abs/pii/S0195666310004381 |
| Payne et al. 2023 -- directional block parameters | https://link.springer.com/article/10.1186/s42234-023-00117-2 |
| PMC4017164 -- general VNS review | https://pmc.ncbi.nlm.nih.gov/articles/PMC4017164/ |
| PMC5063945 -- vagus and obesity | https://pmc.ncbi.nlm.nih.gov/articles/PMC5063945/ |
| PMC8193498 -- VNS and tinnitus | https://pmc.ncbi.nlm.nih.gov/articles/PMC8193498/ |
| vagus.net -- parameters overview | https://vagus.net/understanding-tvns-parameters-and-applications-an-overview/ |
| PMC7199464 -- Critical Review of tVNS | https://pmc.ncbi.nlm.nih.gov/articles/PMC7199464/ |
| PMC9599790 -- tVNS for insomnia | https://pmc.ncbi.nlm.nih.gov/articles/PMC9599790/ |
| Pardo et al. -- weight loss with VNS | https://pmc.ncbi.nlm.nih.gov/articles/PMC2365729/ |
| PMC9859411 -- taVNS and alertness | https://pmc.ncbi.nlm.nih.gov/articles/PMC9859411/ |

### TENS / EMS

| Article | URL |
|---|---|
| Sluka & Walsh 2003 -- TENS mechanisms | https://pubmed.ncbi.nlm.nih.gov/14622708/ |
| Johnson 2007 -- clinical TENS guide | https://pubmed.ncbi.nlm.nih.gov/26526976/ |
| Vance et al. 2014 -- TENS review | https://pubmed.ncbi.nlm.nih.gov/24953072/ |
| Maffiuletti 2010 -- NMES in sports medicine | https://pubmed.ncbi.nlm.nih.gov/20473619/ |
| Filipovic et al. 2012 -- EMS meta-analysis | https://pubmed.ncbi.nlm.nih.gov/22067247/ |
| Kern et al. 2010 -- denervated muscle stimulation | https://pubmed.ncbi.nlm.nih.gov/20460493/ |

### IFT

| Article | URL |
|---|---|
| Fuentes et al. 2010 -- IFT review | https://pubmed.ncbi.nlm.nih.gov/20651012/ |
| Beatti et al. 2011 -- IFT and pain | https://pubmed.ncbi.nlm.nih.gov/22051589/ |
