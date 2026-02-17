    def _format_number(value: float | int) -> str:
        """Format numbers to avoid unnecessary .0 for integers."""
        if isinstance(value, int) or (isinstance(value, float) and value == int(value)):
            return str(int(value))
        return str(value)



    print(_format_number(3)) -> output: 3
print(_format_number(3.4)) -> output: 3.4
print(_format_number(3.0)) -> output: 3



print(f'{3:g}')
print(f'{3.4:g}')
print(f'{3.0:g}')
