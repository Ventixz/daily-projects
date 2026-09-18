<?php

namespace App\Http\Requests;

use Illuminate\Contracts\Validation\ValidationRule;
use Illuminate\Foundation\Http\FormRequest;
use Illuminate\Validation\Rule;

class UpdateBookRequest extends FormRequest
{
    /**
     * Determine if the user is authorized to make this request.
     */
    public function authorize(): bool
    {
        return true;
    }

    /**
     * Get the validation rules that apply to the request.
     *
     * @return array<string, ValidationRule|array<mixed>|string>
     */
    public function rules(): array
    {
        return [
            'title' => ['required', 'string', 'max:255'],
            'author' => ['required', 'string', 'max:255'],
            'isbn' => ['required', 'string', 'max:32', Rule::unique('books', 'isbn')->ignore($this->route('book'))],
            'status' => ['required', Rule::in(['available', 'checked_out'])],
            // Unlike StoreBookRequest, no after_or_equal:today here: a book
            // that's already overdue still needs to be editable (e.g. to
            // finally mark it available again), and rejecting a past due
            // date on every save of an overdue loan would make the record
            // permanently un-editable except through destroy+recreate.
            'due_date' => [
                'nullable',
                'required_if:status,checked_out',
                'prohibited_unless:status,checked_out',
                'date',
            ],
        ];
    }
}
