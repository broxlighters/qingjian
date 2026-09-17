//! 扩展入口只管理配套展示服务的生命周期。
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import {PanelService} from './service.js';

export default class QingjianExtension extends Extension {
    enable() {
        this._service = new PanelService();
    }

    disable() {
        this._service?.destroy();
        this._service = null;
    }
}
