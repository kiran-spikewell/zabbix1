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


namespace Zabbix\Widgets;

use CApiInputValidator,
	DB;

abstract class CWidgetField {

	public const DEFAULT_VIEW = null;

	public const FLAG_ACKNOWLEDGES = 0x01;
	public const FLAG_NOT_EMPTY = 0x02;
	public const FLAG_LABEL_ASTERISK = 0x04;
	public const FLAG_DISABLED = 0x08;

	protected string $name;
	protected ?string $label;
	protected ?string $full_name = null;

	protected ?int $save_type = null;

	protected $value;
	protected $default;

	protected array $values_captions = [];
	protected string $inaccessible_caption = '';

	protected int $max_length;

	protected ?string $action = null;

	protected int $flags = 0x00;

	protected array $validation_rules = [];
	protected ?array $strict_validation_rules = null;
	protected array $ex_validation_rules = [];

	private $templateid = null;

	private bool $default_prevented = false;
	private bool $widget_accepted = false;
	private bool $dashboard_accepted = false;

	private string $in_type = '';

	/**
	 * @param string      $name   Field name in form.
	 * @param string|null $label  Label for the field in form.
	 */
	public function __construct(string $name, string $label = null) {
		$this->name = $name;
		$this->label = $label;
		$this->value = null;
		$this->setSaveType(ZBX_WIDGET_FIELD_TYPE_STR);
	}

	public function getName(): string {
		return $this->name;
	}

	public function getLabel(): ?string {
		return $this->label;
	}

	/**
	 * Set field full name which will appear in case of error messages. For example:
	 * Invalid parameter "<FULL NAME>": too many decimal places.
	 */
	public function setFullName(string $name): self {
		$this->full_name = $name;

		return $this;
	}

	/**
	 * Get field value. If no value is set, return the default value.
	 *
	 * @return mixed
	 */
	public function getValue() {
		return $this->value ?? $this->default;
	}

	public function setValue($value): self {
		$this->value = $value;

		return $this;
	}

	/**
	 * Use actual referred data instead of references. Used for widget presentation.
	 *
	 * Override to customize value structure and/or validation rules.
	 *
	 * @param array $referred_data            Array of substitutions.
	 *        array $foreign_data[]['path']   Path to the reference in the value.
	 *        mixed $foreign_data[]['value']  Value for the reference substitution.
	 *
	 * @return $this
	 */
	public function useReferredData(array $referred_data): self {
		$value = $this->getValue();

		$references = $this->getReferences();
		$resolved_referred_data =  self::resolveReferredData($references, $referred_data);

		foreach ($references as $reference) {
			$source_value = $resolved_referred_data;
			$target_value = &$value;

			foreach ($reference['path'] as $step) {
				if (!is_array($target_value) || !array_key_exists($step, $target_value)) {
					continue 2;
				}

				$source_value = $resolved_referred_data[$step];
				$target_value = &$target_value[$step];
			}

			$target_value = $source_value;
		}

		$this->setValue($value);

		return $this;
	}

	/**
	 * Normalize received referred data according to the declared references.
	 *
	 * Only declared references will be resolved. Missing referred data will resolve as nulls.
	 *
	 * @param array $references
	 * @param array $referred_data
	 *
	 * @return mixed|null
	 */
	protected static function resolveReferredData(array $references, array $referred_data) {
		$resolved_referred_data = null;

		foreach ($references as $reference) {
			$target_value = &$resolved_referred_data;

			foreach ($reference['path'] as $step) {
				if (!is_array($target_value)) {
					$target_value = [];
				}

				$target_value = &$target_value[$step];
			}

			$target_value = null;
		}

		foreach ($referred_data as $entry) {
			$target_value = &$resolved_referred_data;

			foreach ($entry['path'] as $step) {
				if (!is_array($target_value) || !array_key_exists($step, $target_value)) {
					break;
				}

				$target_value = &$target_value[$step];
			}

			$target_value = $entry['value'];
		}

		return $resolved_referred_data;
	}

	public function getValuesCaptions(): array {
		return $this->values_captions;
	}

	public function setValuesCaptions(array $captions): self {
		$values = [];
		$this->toApi($values);

		$inaccessible = 0;
		foreach ($values as $value) {
			if (array_key_exists($value['type'], $captions)) {
				$this->values_captions[$value['value']] = array_key_exists($value['value'], $captions[$value['type']])
					? $captions[$value['type']][$value['value']]
					: [
						'id' => $value['value'],
						'name' => $this->inaccessible_caption.(++$inaccessible > 1 ? ' ('.$inaccessible.')' : ''),
						'inaccessible' => true
					];
			}
		}

		return $this;
	}

	public function setDefault($value): self {
		$this->default = $value;

		return $this;
	}

	public function isDefaultPrevented(): bool {
		return $this->default_prevented;
	}

	/**
	 * Disable exact object selection, like item or host.
	 *
	 * @return $this
	 */
	public function preventDefault($default_prevented = true): self {
		$this->default_prevented = $default_prevented;

		return $this;
	}

	public function isWidgetAccepted(): bool {
		return $this->widget_accepted;
	}

	/**
	 * Allow selecting widget as reference.
	 *
	 * @return $this
	 */
	public function acceptWidget($widget_accepted = true): self {
		$this->widget_accepted = $widget_accepted;

		return $this;
	}

	public function isDashboardAccepted(): bool {
		return $this->dashboard_accepted;
	}

	/**
	 * Allow selecting dashboard as reference.
	 *
	 * @return $this
	 */
	public function acceptDashboard($dashboard_accepted = true): self {
		$this->dashboard_accepted = $dashboard_accepted;

		return $this;
	}

	public function setInType(string $in_type): self {
		$this->in_type = $in_type;

		return $this;
	}

	public function getInType(): string {
		return $this->in_type;
	}

	public function getReferences(): array {
		return [];
	}

	public function getAction(): ?string {
		return $this->action;
	}

	/**
	 * Set JS code that will be called on field change.
	 *
	 * @param string $action  JS function to call on field change.
	 */
	public function setAction(string $action): self {
		$this->action = $action;

		return $this;
	}

	public function getMaxLength(): int {
		return $this->max_length;
	}

	public function setMaxLength(int $max_length): self {
		$this->max_length = $max_length;

		$this->validation_rules['length'] = $this->max_length;

		return $this;
	}

	/**
	 * Get additional flags, which can be used in configuration form.
	 */
	public function getFlags(): int {
		return $this->flags;
	}

	/**
	 * Set additional flags, which can be used in configuration form.
	 */
	public function setFlags(int $flags): self {
		$this->flags = $flags;

		return $this;
	}

	/**
	 * @return int|string|null
	 */
	public function getTemplateId() {
		return $this->templateid;
	}

	public function setTemplateId($templateid): self {
		$this->templateid = $templateid;

		return $this;
	}

	public function isTemplateDashboard(): bool {
		return $this->templateid !== null;
	}

	/**
	 * @param bool $strict  Widget form submit validation?
	 *
	 * @return array  Errors.
	 */
	public function validate(bool $strict = false): array {
		$errors = [];

		$validation_rules = ($strict && $this->strict_validation_rules !== null)
			? $this->strict_validation_rules
			: $this->getValidationRules();
		$validation_rules += $this->ex_validation_rules;

		$value = $this->getValue();

		if ($this->full_name !== null) {
			$label = $this->full_name;
		}
		else {
			$label = $this->label ?? $this->name;
		}

		if (CApiInputValidator::validate($validation_rules, $value, $label, $error)) {
			$this->setValue($value);
		}
		else {
			$this->setValue($this->default);
			$errors[] = $error;
		}

		return $errors;
	}

	/**
	 * Prepares array entry for widget field, ready to be passed to CDashboard API functions.
	 * Reference is needed here to avoid array merging in CWidgetForm::fieldsToApi method. With large number of widget
	 * fields it causes significant performance decrease.
	 *
	 * @param array $widget_fields  reference to Array of widget fields.
	 */
	public function toApi(array &$widget_fields = []): void {
		$value = $this->getValue();

		if ($value !== null && $value !== $this->default) {
			$widget_field = [
				'type' => $this->save_type,
				'name' => $this->name
			];

			if (is_array($value)) {
				foreach ($value as $val) {
					$widget_field['value'] = $val;
					$widget_fields[] = $widget_field;
				}
			}
			else {
				$widget_field['value'] = $value;
				$widget_fields[] = $widget_field;
			}
		}
	}

	protected function setSaveType($save_type): self {
		switch ($save_type) {
			case ZBX_WIDGET_FIELD_TYPE_INT32:
				$this->validation_rules = ['type' => API_INT32];
				break;

			case ZBX_WIDGET_FIELD_TYPE_STR:
				$this->max_length = DB::getFieldLength('widget_field', 'value_str');

				$this->validation_rules = [
					'type' => API_STRING_UTF8,
					'length' => $this->max_length
				];
				break;

			case ZBX_WIDGET_FIELD_TYPE_GROUP:
			case ZBX_WIDGET_FIELD_TYPE_HOST:
			case ZBX_WIDGET_FIELD_TYPE_ITEM:
			case ZBX_WIDGET_FIELD_TYPE_ITEM_PROTOTYPE:
			case ZBX_WIDGET_FIELD_TYPE_GRAPH:
			case ZBX_WIDGET_FIELD_TYPE_GRAPH_PROTOTYPE:
			case ZBX_WIDGET_FIELD_TYPE_MAP:
			case ZBX_WIDGET_FIELD_TYPE_SERVICE:
			case ZBX_WIDGET_FIELD_TYPE_SLA:
			case ZBX_WIDGET_FIELD_TYPE_USER:
			case ZBX_WIDGET_FIELD_TYPE_ACTION:
			case ZBX_WIDGET_FIELD_TYPE_MEDIA_TYPE:
				$this->validation_rules = ['type' => API_IDS];
				break;

			default:
				exit(_('Internal error.'));
		}

		$this->save_type = $save_type;

		return $this;
	}

	protected function getValidationRules(): array {
		return $this->validation_rules;
	}

	protected function setValidationRules(array $validation_rules): self {
		$this->validation_rules = $validation_rules;

		return $this;
	}

	/**
	 * Set validation rules for "strict" mode.
	 */
	protected function setStrictValidationRules(array $strict_validation_rules = null): self {
		$this->strict_validation_rules = $strict_validation_rules;

		return $this;
	}

	protected function setExValidationRules(array $ex_validation_rules): self {
		$this->ex_validation_rules = $ex_validation_rules;

		return $this;
	}

	/**
	 * Set additional flags for validation rule array.
	 */
	protected static function setValidationRuleFlag(array &$validation_rule, int $flag): void {
		if (array_key_exists('flags', $validation_rule)) {
			$validation_rule['flags'] |= $flag;
		}
		else {
			$validation_rule['flags'] = $flag;
		}
	}
}
