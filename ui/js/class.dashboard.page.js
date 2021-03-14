/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


const DASHBOARD_PAGE_STATE_INITIAL = 'initial';
const DASHBOARD_PAGE_STATE_ACTIVE = 'active';
const DASHBOARD_PAGE_STATE_INACTIVE = 'inactive';
const DASHBOARD_PAGE_STATE_DESTROYED = 'destroyed';

const DASHBOARD_PAGE_EVENT_EDIT = 'edit';
const DASHBOARD_PAGE_EVENT_WIDGET_EDIT = 'widget-edit';
const DASHBOARD_PAGE_EVENT_WIDGET_COPY = 'widget-copy';
const DASHBOARD_PAGE_EVENT_WIDGET_DELETE = 'widget-delete';
const DASHBOARD_PAGE_EVENT_WIDGET_DRAG_START = 'widget-drag-start';
const DASHBOARD_PAGE_EVENT_WIDGET_DRAG = 'widget-drag';
const DASHBOARD_PAGE_EVENT_WIDGET_DRAG_END = 'widget-drag-end';
const DASHBOARD_PAGE_EVENT_ANNOUNCE_WIDGETS = 'announce-widgets';
const DASHBOARD_PAGE_EVENT_RESERVE_HEADER_LINES = 'reserve-header-lines';

class CDashboardPage extends CBaseComponent {

	constructor(target, {
		data,
		dashboard,
		cell_width,
		cell_height,
		max_columns,
		max_rows,
		widget_min_rows,
		widget_max_rows,
		widget_defaults,
		is_editable,
		is_edit_mode,
		can_edit_dashboards,
		time_period,
		dynamic_hostid,
		unique_id
	}) {
		super(document.createElement('div'));

		this._dashboard_grid = target;

		this._data = {
			dashboard_pageid: data.dashboard_pageid,
			name: data.name,
			display_period: data.display_period
		};
		this._dashboard = {
			templateid: dashboard.templateid,
			dashboardid: dashboard.dashboardid
		};
		this._cell_width = cell_width;
		this._cell_height = cell_height;
		this._max_columns = max_columns;
		this._max_rows = max_rows;
		this._widget_min_rows = widget_min_rows;
		this._widget_max_rows = widget_max_rows;
		this._widget_defaults = widget_defaults;
		this._is_editable = is_editable;
		this._is_edit_mode = is_edit_mode;
		this._can_edit_dashboards = can_edit_dashboards;
		this._time_period = time_period;
		this._dynamic_hostid = dynamic_hostid;
		this._unique_id = unique_id;

		this._init();
		this._registerEvents();
	}

	_init() {
		this._state = DASHBOARD_PAGE_STATE_INITIAL;

		this._widgets = new Map();

		if (this._is_edit_mode) {
			this._initWidgetDragging();
		}
	}

	_registerEvents() {
		this._events = {
			widgetEdit: (e) => {
				const widget = e.detail.target;

				if (!this._is_edit_mode) {
					this.setEditMode();
					this.fire(DASHBOARD_PAGE_EVENT_EDIT);
				}

				this.fire(DASHBOARD_PAGE_EVENT_WIDGET_EDIT, {widget});
			},

			widgetEnter: (e) => {
				const widget = e.detail.target;

				if (widget.isEntered() || this._isInteracting()) {
					return;
				}

				widget.enter();
				this.leaveWidgetsExcept(widget);
				this._reserveHeaderLines();
			},

			widgetLeave: (e) => {
				const widget = e.detail.target;

				if (!widget.isEntered() || this._isInteracting()) {
					return;
				}

				widget.leave();
				this._reserveHeaderLines();
			},

			widgetCopy: (e) => {
				const widget = e.detail.target;

				this.fire(DASHBOARD_PAGE_EVENT_WIDGET_COPY, {data: widget.getDataCopy()});
			},

			widgetPaste: (e) => {
				const widget = e.detail.target;

//				if (!widget.isEntered() || this._isInteracting()) {
//					return;
//				}

//				widget.leave();
//				this._reserveHeaderLines();
			},

			widgetDelete: (e) => {
				const widget = e.detail.target;

				this.deleteWidget(widget);
			}
		};
	}

	/**
	 * Find free position of the given width and height.
	 *
	 * @param {int} width
	 * @param {int} height
	 *
	 * @returns {object|null}
	 */
	findFreePos({width, height}) {
		const pos = {x: 0, y: 0, width: width, height: height};

		// Go y by row and try to position widget in each space.
		const max_column = this._max_columns - pos.width;
		const max_row = this._max_rows - pos.height;

		let found = false;
		let x, y;

		for (y = 0; !found; y++) {
			if (y > max_row) {
				return null;
			}
			for (x = 0; x <= max_column && !found; x++) {
				pos.x = x;
				pos.y = y;
				found = this.isPosFree(pos);
			}
		}

		return pos;
	}

	isPosFree(pos) {
		for (const widget of this._widgets.keys()) {
			if (this._isOverlappingPos(pos, widget.getPosition())) {
				return false;
			}
		}

		return true;
	}

	accommodatePos(pos, {reverse_x = false, reverse_y = false} = {}) {
		let pos_variants = [];

		let pos_x = this._accommodatePosX({
			...pos,
			y: reverse_y ? pos.y + pos.height - this._widget_min_rows : pos.y,
			height: this._widget_min_rows
		}, {reverse: reverse_x});

		pos_x = {...pos_x, y: pos.y, height: pos.height};

		if (reverse_x) {
			for (let x = pos_x.x, width = pos_x.width; width >= 1; x++, width--) {
				pos_variants.push(this._accommodatePosY({...pos_x, x, width}, {reverse: reverse_y}));
			}
		}
		else {
			for (let width = pos_x.width; width >= 1; width--) {
				pos_variants.push(this._accommodatePosY({...pos_x, width}, {reverse: reverse_y}));
			}
		}

		let pos_best = null;
		let pos_best_value = null;

		for (const pos_variant of pos_variants) {
			const delta_x = Math.abs(reverse_x ? pos_variant.x - pos.x : pos_variant.width - pos.width);
			const delta_y = Math.abs(reverse_y ? pos_variant.y - pos.y : pos_variant.height - pos.height);
			const value = Math.sqrt(Math.pow(delta_x, 2) + Math.pow(delta_y, 2));

			if (pos_best === null
					|| (pos_best.width == 1 && pos_variant.width > 1)
					|| ((pos_best.width > 1 === pos_variant.width > 1) && value < pos_best_value)) {
				pos_best = {...pos_variant};
				pos_best_value = value;
			}
		}

		return pos_best;
	}

	_accommodatePosX(pos, {reverse = false} = {}) {
		const max_pos = {...pos};

		if (reverse) {
			for (let x = pos.x + pos.width - 1, width = 1; x >= pos.x; x--, width++) {
				if (!this.isPosFree({...max_pos, x, width})) {
					break;
				}

				max_pos.x = x;
				max_pos.width = width;
			}
		}
		else {
			for (let width = 1; width <= pos.width; width++) {
				if (!this.isPosFree({...max_pos, width})) {
					break;
				}

				max_pos.width = width;
			}
		}

		return max_pos;
	}

	_accommodatePosY(pos, {reverse = false} = {}) {
		const max_pos = {...pos};

		if (reverse) {
			for (let y = pos.y + pos.height - 1, height = 1; y >= pos.y; y--, height++) {
				if (!this.isPosFree({...max_pos, y, height})) {
					break;
				}

				max_pos.y = y;
				max_pos.height = height;
			}
		}
		else {
			for (let height = this._widget_min_rows; height <= pos.height; height++) {
				if (!this.isPosFree({...max_pos, height})) {
					break;
				}

				max_pos.height = height;
			}
		}

		return max_pos;
	}

	/**
	 * Check if positions overlap.
	 *
	 * @param {object} pos_1
	 * @param {object} pos_2
	 *
	 * @returns {boolean}
	 */
	_isOverlappingPos(pos_1, pos_2) {
		return (
			pos_1.x < (pos_2.x + pos_2.width) && (pos_1.x + pos_1.width) > pos_2.x
				&& pos_1.y < (pos_2.y + pos_2.height) && (pos_1.y + pos_1.height) > pos_2.y
		);
	}

	_isEqualPos(pos_1, pos_2) {
		return (pos_1.x === pos_2.x && pos_1.y === pos_2.y
			&& pos_1.width === pos_2.width && pos_1.height === pos_2.height);
	}

	getData() {
		return this._data;
	}

	getWidgets() {
		return Array.from(this._widgets.keys());
	}

	getWidget(unique_id) {
		for (const widget of this._widgets.keys()) {
			if (widget.getUniqueId() === unique_id) {
				return widget;
			}
		}

		return null;
	}

	getUniqueId() {
		return this._unique_id;
	}

	isUpdated() {

	}

	editProperties() {

	}

	applyProperties() {

	}

	_reserveHeaderLines() {
		let num_header_lines = 0;

		for (const widget of this._widgets.keys()) {
			if (!widget.isEntered()) {
				continue;
			}

			if (widget.getPosition().y != 0) {
				break;
			}

			num_header_lines = widget.getNumHeaderLines();
		}

		this.fire(DASHBOARD_PAGE_EVENT_RESERVE_HEADER_LINES, {num_header_lines: num_header_lines});
	}

	leaveWidgetsExcept(except_widget = null) {
		for (const widget of this._widgets.keys()) {
			if (widget !== except_widget) {
				widget.leave();
			}
		}
	}

	_isInteracting() {
		for (const widget of this._widgets.keys()) {
			const widget_view = widget.getView();

			if (widget.isInteracting()
					|| widget_view.classList.contains('ui-draggable-dragging')
					|| widget_view.classList.contains('ui-resizable-resizing')) {
				return true;
			}
		}

		return false;
	}

	start() {
		this._state = DASHBOARD_PAGE_STATE_INACTIVE;

		for (const widget of this._widgets.keys()) {
			widget.start();
		}
	}

	activate() {
		this._state = DASHBOARD_PAGE_STATE_ACTIVE;

		for (const widget of this._widgets.keys()) {
			this._dashboard_grid.appendChild(widget.getView());
			this._activateWidget(widget);
		}

		if (this._is_edit_mode) {
			this._activateWidgetDragging();
		}
	}

	_activateWidget(widget) {
		widget.activate();
		widget
			.on(WIDGET_EVENT_EDIT, this._events.widgetEdit)
			.on(WIDGET_EVENT_ENTER, this._events.widgetEnter)
			.on(WIDGET_EVENT_LEAVE, this._events.widgetLeave)
			.on(WIDGET_EVENT_COPY, this._events.widgetCopy)
			.on(WIDGET_EVENT_PASTE, this._events.widgetPaste)
			.on(WIDGET_EVENT_DELETE, this._events.widgetDelete);
	}

	deactivate() {
		this._state = DASHBOARD_PAGE_STATE_INACTIVE;

		for (const widget of this._widgets.keys()) {
			this._dashboard_grid.removeChild(widget.getView());
			this._deactivateWidget(widget);
		}

		if (this._is_edit_mode) {
			this._dectivateWidgetDragging();
		}
	}

	_deactivateWidget(widget) {
		widget.deactivate();
		widget
			.off(WIDGET_EVENT_EDIT, this._events.widgetEdit)
			.off(WIDGET_EVENT_ENTER, this._events.widgetEnter)
			.off(WIDGET_EVENT_LEAVE, this._events.widgetLeave)
			.off(WIDGET_EVENT_COPY, this._events.widgetCopy)
			.off(WIDGET_EVENT_PASTE, this._events.widgetPaste)
			.off(WIDGET_EVENT_DELETE, this._events.widgetDelete);
	}

	destroy() {
		if (this._state === DASHBOARD_PAGE_STATE_ACTIVE) {
			this.deactivate();
		}
		if (this._state !== DASHBOARD_PAGE_STATE_INACTIVE) {
			throw new Error('Unsupported state change.');
		}
		this._state = DASHBOARD_PAGE_STATE_DESTROYED;

		for (const widget of this._widgets.keys()) {
			widget.destroy();
		}

		this._widgets.clear();
	}

	resize() {
		for (const widget of this._widgets.keys()) {
			widget.resize();
		}
	}

	getState() {
		return this._state;
	}

	isEditMode() {
		return this._is_edit_mode;
	}

	setEditMode() {
		this._is_edit_mode = true;

		for (const widget of this._widgets.keys()) {
			widget.setEditMode();
		}

		this._initWidgetDragging();

		if (this._state === DASHBOARD_PAGE_STATE_ACTIVE) {
			this._activateWidgetDragging();
		}
	}

	setDynamicHost(dynamic_hostid) {
		if (this._dynamic_hostid != dynamic_hostid) {
			this._dynamic_hostid = dynamic_hostid;

			for (const widget of this._widgets.keys()) {
				if (widget.supportsDynamicHosts() && this._dynamic_hostid != widget.getDynamicHost()) {
					widget.setDynamicHost(this._dynamic_hostid);
				}
			}
		}
	}

	setTimePeriod(time_period) {
		this._time_period = time_period;

		for (const widget of this._widgets.keys()) {
			widget.setTimePeriod(this._time_period);
		}
	}

	getNumRows() {
		let num_rows = 0;

		for (const widget of this._widgets.keys()) {
			const pos = widget.getPosition();

			num_rows = Math.max(num_rows, pos.y + pos.height);
		}

		return num_rows;
	}

	addWidget({type, name, view_mode, fields, configuration, widgetid, pos, is_new, rf_rate, unique_id}) {
		const widget = new (eval(this._widget_defaults[type].js_class))({
			type,
			name,
			view_mode,
			fields,
			configuration,
			defaults: this._widget_defaults[type],
			parent: null,
			widgetid,
			pos,
			is_new,
			rf_rate,
			dashboard: {
				templateid: this._dashboard.templateid,
				dashboardid: this._dashboard.dashboardid
			},
			dashboard_page: {
				unique_id: this._unique_id
			},
			cell_width: this._cell_width,
			cell_height: this._cell_height,
			is_editable: this._is_editable,
			is_edit_mode: this._is_edit_mode,
			can_edit_dashboards: this._can_edit_dashboards,
			time_period: this._time_period,
			dynamic_hostid: this._dynamic_hostid,
			unique_id
		});

		this._widgets.set(widget, {});

		this.fire(DASHBOARD_PAGE_EVENT_ANNOUNCE_WIDGETS);

		if (this._state !== DASHBOARD_PAGE_STATE_INITIAL) {
			widget.start();
		}

		if (this._state === DASHBOARD_PAGE_STATE_ACTIVE) {
			this._dashboard_grid.appendChild(widget.getView());
			this._activateWidget(widget);
		}

		return widget;
	}

	deleteWidget(widget, {is_batch_mode = false} = {}) {
		if (widget.getState() === WIDGET_STATE_ACTIVE) {
			this._dashboard_grid.removeChild(widget.getView());
			this._deactivateWidget(widget);
		}

		if (widget.getState() !== WIDGET_STATE_INITIAL) {
			widget.destroy();
		}

		this._widgets.delete(widget);

		if (!is_batch_mode) {
			this.fire(DASHBOARD_PAGE_EVENT_WIDGET_DELETE);
			this.fire(DASHBOARD_PAGE_EVENT_ANNOUNCE_WIDGETS);
		}
	}

	replaceWidget(widget, widget_data) {
		this.deleteWidget(widget, {is_batch_mode: true});
		this.addWidget(widget_data);
	}

	announceWidgets(dashboard_pages) {
		let widgets = [];

		for (const dashboard_page of dashboard_pages) {
			widgets = widgets.concat(Array.from(dashboard_page._widgets.keys()));
		}

		for (const widget of widgets) {
			widget.announceWidgets(widgets);
		}
	}

	_initWidgetDragging() {
		const widget_helper = document.createElement('div');

		widget_helper.classList.add('dashbrd-grid-widget-placeholder');
		widget_helper.append(document.createElement('div'));

		let dragging_widget = null;
		let dragging_pos = null;
		let drag_rel_x = null;
		let drag_rel_y = null;

		const showWidgetHelper = (pos) => {
			this._dashboard_grid.prepend(widget_helper);

			widget_helper.style.left = `${this._cell_width * pos.x}%`;
			widget_helper.style.top = `${this._cell_height * pos.y}px`;
			widget_helper.style.width = `${this._cell_width * pos.width}%`;
			widget_helper.style.height = `${this._cell_height * pos.height}px`;
		};

		const hideWidgetHelper = () => {
			widget_helper.remove();
		};

		const pullWidgetsUp = (widgets, max_delta) => {
			do {
				let widgets_below = [];

				for (const widget of widgets) {
					const data = this._widgets.get(widget);

					for (let y = Math.max(0, data.pos.y - max_delta); y < data.pos.y; y++) {
						const test_pos = {...data.pos, y};

						if (isDataPosFree(test_pos, {except_widget: widget})) {
							for (const [w, w_data] of this._widgets) {
								if (w === widget || w === dragging_widget) {
									continue;
								}

								if (this._isOverlappingPos(w_data.pos, {...data.pos, height: data.pos.height + 1})) {
									widgets_below.push(w);
								}
							}

							data.pos = test_pos;
							break;
						}
					}
				}

				widgets = widgets_below;
			}
			while (widgets.length > 0);
		};

		const relocateWidget = (widget, pos) => {
			if (pos.y + pos.height > this._max_rows) {
				return false;
			}

			for (const [w, data] of this._widgets) {
				if (w === widget || w === dragging_widget) {
					continue;
				}

				if (this._isOverlappingPos(data.pos, pos)) {
					const test_pos = {...data.pos, y: pos.y + pos.height};

					if (!relocateWidget(w, test_pos)) {
						return false;
					}

					data.pos = test_pos;
				}
			}

			return true;
		};

		const isDataPosFree = (pos, {except_widget = null} = {}) => {
			for (const [widget, data] of this._widgets) {
				if (widget === except_widget || widget === dragging_widget) {
					continue;
				}

				if (this._isOverlappingPos(data.pos, pos)) {
					return false;
				}
			}

			return true;
		};

		const allocatePos = (widget, pos) => {
			for (const [w, w_data] of this._widgets) {
				w_data.pos = w_data.original_pos;
			}

			const data = this._widgets.get(widget);
			const original_pos = data.original_pos;

			let widgets_below = [];

			for (const [w, w_data] of this._widgets) {
				if (w === widget || w === dragging_widget) {
					continue;
				}

				if (this._isOverlappingPos(w_data.pos, {...original_pos, height: original_pos.height + 1})) {
					widgets_below.push(w);
				}
			}

			pullWidgetsUp(widgets_below, original_pos.height);

			const result = relocateWidget(widget, pos);

			for (const [w, w_data] of this._widgets) {
				if (result && w !== widget) {
					w.setPosition(w_data.pos);
				}

				delete w_data.pos;
			}

			return result;
		};

		const events = {
			mouseDown: (e) => {
				dragging_widget = null;

				for (const widget of this._widgets.keys()) {
					const widget_view = widget.getView();

					if (widget_view.querySelector(`.${widget.getCssClass('head')}`).contains(e.target)
							&& !widget_view.querySelector(`.${widget.getCssClass('actions')}`).contains(e.target)) {
						dragging_widget = widget;
						break;
					}
				}

				if (dragging_widget === null) {
					return;
				}

				this.fire(DASHBOARD_PAGE_EVENT_WIDGET_DRAG_START);

				dragging_pos = dragging_widget.getPosition();

				for (const [widget, data] of this._widgets) {
					data.original_pos = widget.getPosition();
				}

				const widget_view = dragging_widget.getView();
				const widget_view_computed_style = getComputedStyle(widget_view);

				widget_view.classList.add('ui-draggable-dragging');

				drag_rel_x = parseInt(widget_view_computed_style.getPropertyValue('left')) - e.clientX;
				drag_rel_y = parseInt(widget_view_computed_style.getPropertyValue('top')) - e.clientY
					- document.querySelector('.wrapper').scrollTop;

				document.addEventListener('mouseup', events.mouseUp, {passive: false});
				document.addEventListener('mousemove', events.mouseMove);
				this._dashboard_grid.removeEventListener('mousemove', events.mouseMove);

				e.preventDefault();
			},

			mouseUp: (e) => {
				dragging_widget.getView().classList.remove('ui-draggable-dragging');
				dragging_widget.setPosition(dragging_pos);
				hideWidgetHelper();

				dragging_widget = null;
				dragging_pos = null;
				drag_rel_x = null;
				drag_rel_y = null;

				for (const data of this._widgets.values()) {
					delete data.original_position;
				}

				document.removeEventListener('mouseup', events.mouseUp);
				document.removeEventListener('mousemove', events.mouseMove);
				this._dashboard_grid.addEventListener('mousemove', events.mouseMove);

				this.fire(DASHBOARD_PAGE_EVENT_WIDGET_DRAG_END);
			},

			mouseMove: (e) => {
				if (dragging_widget === null) {
					return;
				}

				const grid_rect = this._dashboard_grid.getBoundingClientRect();

				const widget_pos = dragging_widget.getPosition();
				const widget_view = dragging_widget.getView();
				const widget_view_rect = widget_view.getBoundingClientRect();

				const pos_left = Math.max(0, Math.min(grid_rect.width - widget_view_rect.width, e.pageX + drag_rel_x));
				const pos_top = Math.max(0, Math.min(grid_rect.height - widget_view_rect.height,
					e.pageY + drag_rel_y + document.querySelector('.wrapper').scrollTop
				));

				widget_view.style.left = `${pos_left}px`;
				widget_view.style.top = `${pos_top}px`;

				const pos = this._getGridPos({
					x: pos_left,
					y: pos_top,
					width: widget_pos.width,
					height: widget_pos.height
				});

				if (!this._isEqualPos(pos, dragging_pos)) {
					if (allocatePos(dragging_widget, pos)) {
						dragging_pos = pos;
						showWidgetHelper(dragging_pos);

						this.fire(DASHBOARD_PAGE_EVENT_WIDGET_DRAG, {pos: dragging_pos});
					}
				}
			}
		};

		this._dashboard_grid.addEventListener('mousedown', events.mouseDown, {passive: false});
		this._dashboard_grid.addEventListener('mousemove', events.mouseMove);
	}

	_activateWidgetDragging() {
	}

	_deactivateWidgetDragging() {
	}

	_getGridPos({x, y, width, height}) {
		const rect = this._dashboard_grid.getBoundingClientRect();

		const pos_x = parseInt(x / rect.width * this._max_columns + 0.5);
		const pos_y = parseInt(y / this._cell_height + 0.5);

		return {
			x: Math.max(0, Math.min(this._max_columns - width, pos_x)),
			y: Math.max(0, Math.min(this._max_rows - height, pos_y)),
			width,
			height
		};
	}
}
