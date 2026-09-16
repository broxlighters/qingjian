//! Linux 协议帧到共享展示模型的转换，不查询词库、不改变候选顺序或提交文本。
use crate::sense::SenseRange;
use qingjian_core::CandidateKind;
use qingjian_platform::protocol::{Frame as ProtocolFrame, PreeditKind};
use qingjian_render::{Frame, Preedit, PreeditSegment, PreeditStyle, Row, Tone};

pub(crate) struct DisplayFrame {
    pub frame: Frame,

    pub senses: Vec<SenseRange>,
}
impl DisplayFrame {
    pub fn new(source: &ProtocolFrame) -> Self {
        let mut senses = Vec::new();
        let rows = source
            .candidates
            .items
            .iter()
            .enumerate()
            .map(|(index, candidate)| {
                let mut row = Row::plain(index, &candidate.text);
                row.cloud = candidate.kind == CandidateKind::Cloud;
                if candidate.text.is_empty() {
                    return row;
                }
                if let Some(reading) = &candidate.reading {
                    row.annotation.push((reading.clone(), Tone::Gloss));
                }
                if let Some(translation) = &candidate.translation {
                    for (sense_index, sense) in translation.senses().iter().enumerate() {
                        if !row.annotation.is_empty() {
                            row.annotation.push((" · ".into(), Tone::Faint));
                        }
                        let start = row.annotation.len();
                        if let Some(pos) = sense.part_of_speech {
                            row.annotation.push((format!("{pos} "), Tone::Faint));
                        }
                        let tone = if sense.fresh {
                            Tone::Fresh
                        } else {
                            Tone::Gloss
                        };
                        for segment in sense.furigana() {
                            if !segment.text.is_empty() {
                                row.annotation.push((segment.text, tone));
                            }
                            if let Some(reading) = segment.reading {
                                row.annotation.push((format!("({reading})"), Tone::Faint));
                            }
                        }
                        if !sense.text.is_empty() {
                            senses.push(SenseRange {
                                row: index,
                                sense: sense_index,
                                segments: start..row.annotation.len(),
                            });
                        }
                    }
                }
                row
            })
            .collect();
        let frame = Frame {
            preedit: (!source.preedit.is_empty()).then(|| Preedit {
                segments: source
                    .preedit
                    .iter()
                    .map(|segment| PreeditSegment {
                        text: segment.text.clone(),
                        style: match segment.kind {
                            PreeditKind::Typed => PreeditStyle::Typed,
                            PreeditKind::Rest => PreeditStyle::Rest,
                            PreeditKind::Corrected => PreeditStyle::Struck,
                        },
                    })
                    .collect(),
                cursor: source.cursor,
            }),
            rows,
            highlighted: Some(source.highlight),
            footer: None,
            sentence: source.sentence.clone(),
            status: source.notice.clone(),
        };
        Self { frame, senses }
    }
}
