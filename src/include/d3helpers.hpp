#pragma once

struct ScopedSchema
{
    ScopedSchema()
    {
        reset();
    }
    ~ScopedSchema()
    {
        free(const_cast<char*>(schema.engineName));
        free(const_cast<char*>(schema.engineVersion));
        free(const_cast<char*>(schema.info));
        for (size_t i = 0; i < schema.channels.nChannels; ++i)
            free(const_cast<char*>(schema.channels.channels[i]));
        free(schema.channels.channels);
        for (size_t i = 0; i < schema.scenes.nScenes; ++i)
        {
            RemoteParameters& scene = schema.scenes.scenes[i];
            free(const_cast<char*>(scene.name));
            for (size_t j = 0; j < scene.nParameters; ++j)
            {
                RemoteParameter& parameter = scene.parameters[j];
                free(const_cast<char*>(parameter.group));
                free(const_cast<char*>(parameter.displayName));
                free(const_cast<char*>(parameter.key));
                if (parameter.type == RS_PARAMETER_TEXT)
                    free(const_cast<char*>(parameter.defaults.text.defaultValue));
                for (size_t k = 0; k < parameter.nOptions; ++k)
                {
                    free(const_cast<char*>(parameter.options[k]));
                }
                free(parameter.options);
            }
            free(scene.parameters);
        }
        free(schema.scenes.scenes);
        reset();
    }
    void reset()
    {
        schema.engineName = nullptr;
        schema.engineVersion = nullptr;
        schema.info = nullptr;
        schema.channels.nChannels = 0;
        schema.channels.channels = nullptr;
        schema.scenes.nScenes = 0;
        schema.scenes.scenes = nullptr;
    }
    ScopedSchema(const ScopedSchema&) = delete;
    ScopedSchema(ScopedSchema&& other)
    {
        schema = std::move(other.schema);
        other.reset();
    }
    ScopedSchema& operator=(const ScopedSchema&) = delete;
    ScopedSchema& operator=(ScopedSchema&& other)
    {
        schema = std::move(other.schema);
        other.reset();
        return *this;
    }

    Schema schema;
};
