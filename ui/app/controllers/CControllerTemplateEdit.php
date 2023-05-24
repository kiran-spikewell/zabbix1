<?php declare(strict_types = 0);
/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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


require_once __DIR__ .'/../../include/forms.inc.php';

class CControllerTemplateEdit extends CController {

	/**
	 * @var mixed
	 */
	private $template = [];

	protected function init(): void {
		$this->disableCsrfValidation();
	}

	protected function checkInput(): bool {
		$fields = [
			'templateid' =>	'',
			'groupids' =>	''
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(
				(new CControllerResponseData(['main_block' => json_encode([
					'error' => [
						'messages' => array_column(get_and_clear_messages(), 'message')
					]
				])]))->disableView()
			);
		}

		return $ret;
	}

	protected function checkPermissions(): bool {
		if (!$this->checkAccess(CRoleHelper::UI_CONFIGURATION_TEMPLATES)) {
			return false;
		}

		if ($this->hasInput('templateid')) {
			// todo - check if all output elements are needed here.
			$templates = API::Template()->get([
				'output' => API_OUTPUT_EXTEND,
				'templateids' => $this->getInput('templateid'),
				'editable' => true
			]);

			if (!$templates) {
				return false;
			}

			$this->template = $templates[0];
		}

		return true;
	}

	protected function doAction(): void {
		$templateid = $this->getInput('templateid');

//		if ($this->getInput('templateid') === null) {
//			$dbTemplates = API::Template()->get([
//				'output' => API_OUTPUT_EXTEND,
//				'selectTemplateGroups' => ['groupid'],
//				'selectParentTemplates' => ['templateid', 'name'],
//				'selectMacros' => API_OUTPUT_EXTEND,
//				'selectTags' => ['tag', 'value'],
//				'selectValueMaps' => ['valuemapid', 'name', 'mappings'],
//				'templateids' => $templateid
//			]);
//			$data['dbTemplate'] = reset($dbTemplates);
//		}

		// temporary data array:
		$data = [
			'templateid' => getRequest('templateid', 0),
			'template_name' => '',
			'visible_name' => '',
			'description' => '',
			'groups_ms' => [],
			'linked_templates' => [],
			'add_templates' => [],
			'original_templates' => [],
			'show_inherited_macros' => false,
			'vendor' => [],
			'readonly' => false,
			'macros' => [],
			'valuemaps' => []
		];

	//	$data = array_merge($this->template, $data);

//		// description
//		$data['description'] = ($data['templateid'] !== null)
//			? $data['dbTemplate']['description']
//			: getRequest('description', '');

		// tags
		if (!array_key_exists('tags', $data)) {
			$data['tags'][] = ['tag' => '', 'value' => ''];
		}
		else {
			CArrayHelper::sort($data['tags'], ['tag', 'value']);
		}

//		// Add already linked and new templates.
//		$templates = [];
//		$request_linked_templates = getRequest('templates', hasRequest('form_refresh') ? [] : $data['original_templates']);
//		$request_add_templates = getRequest('add_templates', []);

//		if ($request_linked_templates || $request_add_templates) {
//			$templates = API::Template()->get([
//				'output' => ['templateid', 'name'],
//				'templateids' => array_merge($request_linked_templates, $request_add_templates),
//				'preservekeys' => true
//			]);

//			$data['linked_templates'] = array_intersect_key($templates, array_flip($request_linked_templates));
//			CArrayHelper::sort($data['linked_templates'], ['name']);

//			$data['add_templates'] = array_intersect_key($templates, array_flip($request_add_templates));

//			foreach ($data['add_templates'] as &$template) {
//				$template = CArrayHelper::renameKeys($template, ['templateid' => 'id']);
//			}
//			unset($template);
//		}

//		$data['writable_templates'] = API::Template()->get([
//			'output' => ['templateid'],
//			'templateids' => array_keys($data['linked_templates']),
//			'editable' => true,
//			'preservekeys' => true
//		]);

		// Add inherited macros to template macros.
		if ($data['show_inherited_macros']) {
			$data['macros'] = mergeInheritedMacros($data['macros'], getInheritedMacros(array_keys($templates)));
		}

		// Sort only after inherited macros are added. Otherwise the list will look chaotic.
		$data['macros'] = array_values(order_macros($data['macros'], 'macro'));

		// The empty inputs will not be shown if there are inherited macros, for example.
		if (!$data['macros']) {
			$macro = ['macro' => '', 'value' => '', 'description' => '', 'type' => ZBX_MACRO_TYPE_TEXT];

			if ($data['show_inherited_macros']) {
				$macro['inherited_type'] = ZBX_PROPERTY_OWN;
			}

			$data['macros'][] = $macro;
		}

		// Add inherited macros to template macros.
		if ($data['show_inherited_macros']) {
			$data['macros'] = mergeInheritedMacros($data['macros'], getInheritedMacros(array_keys($templates)));
		}

		foreach ($data['macros'] as &$macro) {
			$macro['discovery_state'] = CControllerHostMacrosList::DISCOVERY_STATE_MANUAL;
		}
		unset($macro);

		if ($templateid !== null) {
			$groups = array_column($data['dbTemplate']['templategroups'], 'groupid');
		}
		else {
			$groups = $this->hasInput('groupids') ? $this->getInput('groupids') : [];
		}

		$groupids = [];

		foreach ($groups as $group) {
			if (is_array($group) && array_key_exists('new', $group)) {
				continue;
			}

			$groupids[] = $group;
		}

		// Groups with R and RW permissions.
		$groups_all = $groupids
			? API::TemplateGroup()->get([
				'output' => ['name'],
				'groupids' => $groupids,
				'preservekeys' => true
			])
			: [];

		// Groups with RW permissions.
		$groups_rw = $groupids && (CWebUser::getType() != USER_TYPE_SUPER_ADMIN)
			? API::TemplateGroup()->get([
				'output' => [],
				'groupids' => $groupids,
				'editable' => true,
				'preservekeys' => true
			])
			: [];

		// Prepare data for multiselect.
		foreach ($groups as $group) {
			if (is_array($group) && array_key_exists('new', $group)) {
				$data['groups_ms'][] = [
					'id' => $group['new'],
					'name' => $group['new'].' ('._x('new', 'new element in multiselect').')',
					'isNew' => true
				];
			}
			elseif (array_key_exists($group, $groups_all)) {
				$data['groups_ms'][] = [
					'id' => $group,
					'name' => $groups_all[$group]['name'],
					'disabled' => (CWebUser::getType() != USER_TYPE_SUPER_ADMIN) && !array_key_exists($group, $groups_rw)
				];
			}
		}

		CArrayHelper::sort($data['groups_ms'], ['name']);

		// This data is used in template.edit.js.php.
		$data['macros_tab'] = [
			'linked_templates' => array_map('strval', array_keys($data['linked_templates'])),
			'add_templates' => array_map('strval', array_keys($data['add_templates']))
		];


		$templateids = getRequest('templates', []);
		$clear_templates = getRequest('clear_templates', []);

		$clear_templates = array_intersect($clear_templates, array_keys($data['original_templates']));
		$clear_templates = array_diff($clear_templates, array_keys($templateids));

		$data['clear_templates'] = $clear_templates;
		$data['user'] = ['debug_mode' => $this->getDebugMode()];
		$response = new CControllerResponseData($data);
		$this->setResponse($response);
	}
}
